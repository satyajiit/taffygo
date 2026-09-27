// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/model/profile_model_broker.h"

#include <algorithm>
#include <limits>
#include <utility>

#include "base/functional/bind.h"
#include "base/location.h"
#include "base/task/sequenced_task_runner.h"
#include "base/time/time.h"
#include "mojo/public/cpp/bindings/callback_helpers.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "taffy/browser/core_model_effect_validation.h"
#include "taffy/browser/managed_route_configuration.h"
#include "taffy/browser/model/profile_model_broker_route.h"
#include "taffy/browser/profile_page_media_store.h"

namespace taffy {
namespace {

namespace service = core_service::mojom;

uint64_t NowMonotonicMillisForModel() {
  const int64_t value = base::TimeTicks::Now().since_origin().InMilliseconds();
  return value < 0 ? 0u : static_cast<uint64_t>(value);
}

}  // namespace

ProfileModelBroker::PendingCall::PendingCall() = default;
ProfileModelBroker::PendingCall::~PendingCall() = default;

ProfileModelBroker::ProfileModelBroker(
    scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory,
    std::string managed_worker_origin)
    : url_loader_factory_(std::move(url_loader_factory)),
      managed_worker_origin_(std::move(managed_worker_origin)) {}

ProfileModelBroker::~ProfileModelBroker() = default;

void ProfileModelBroker::SetCredentialResolver(CredentialResolver resolver) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  credential_resolver_ = std::move(resolver);
}

void ProfileModelBroker::SetEntitlementTokenProvider(
    EntitlementTokenProvider provider) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  entitlement_token_provider_ = std::move(provider);
}

void ProfileModelBroker::SetQuotaRefusedCallback(
    base::RepeatingClosure callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  quota_refused_callback_ = std::move(callback);
}

void ProfileModelBroker::SetModelStreamChunkDispatcher(
    ModelStreamChunkDispatcher dispatcher) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  model_stream_chunk_dispatcher_ = std::move(dispatcher);
}

void ProfileModelBroker::SetPageMediaStore(
    scoped_refptr<ProfilePageMediaStore> store) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  page_media_store_ = std::move(store);
}

void ProfileModelBroker::SetTransientCredentialConsumer(
    TransientCredentialConsumer consumer) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  transient_credential_consumer_ = std::move(consumer);
}

void ProfileModelBroker::SetRegisteredEndpointLookup(
    RegisteredEndpointLookup lookup) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  registered_endpoint_lookup_ = std::move(lookup);
}

size_t ProfileModelBroker::in_flight_count_for_testing() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return in_flight_.size();
}

void ProfileModelBroker::Dispatch(service::EffectEnvelopePtr effect,
                                  EffectCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!effect || !effect->operation || !effect->model_request) {
    // A body this class cannot read at all. The effect broker still holds the
    // envelope and makes the terminal from it, which is a better answer than
    // one synthesized from a record that is missing the fields it would need.
    std::move(callback).Run(nullptr);
    return;
  }

  // Called again even though `CoreEffectBroker` validated the envelope before
  // dispatching it. This is not a second copy of the rules — it is the same
  // function — and it is what makes the class safe for any caller rather than
  // safe because of who happens to call it today. Two of those rules are the
  // reason: the endpoint is one of exactly two things — an https origin the
  // catalog named, or the string this browser wrote down when a person typed
  // it — and no static header names a place a credential travels in.
  //
  // This is also the one call site that asks the register form, because this
  // is the one place a request is sent from. An effect claiming a person's
  // own address is accepted only when the register holds that exact string
  // for its provider (decision 0096 section 1); upstream the same request was
  // bounded and left undecided, which is why the register is passed here and
  // nowhere above.
  if (!IsValidCoreModelRequest(
          *effect->model_request, service::kMaxIdentifierBytes,
          service::kMaxEffectBytes, registered_endpoint_lookup_)) {
    Refuse(*effect, std::move(callback), service::EffectStatus::kDenied);
    return;
  }

  const service::ModelRequestEffect& request = *effect->model_request;
  if (!StaticHeadersAreCarryable(request.static_headers)) {
    Refuse(*effect, std::move(callback), service::EffectStatus::kDenied);
    return;
  }

  const std::optional<model_broker::ModelRoute> route =
      RouteFor(request.wire_api, request.provider_id, request.model_id);
  if (!route) {
    Refuse(*effect, std::move(callback), service::EffectStatus::kDenied);
    return;
  }
  const bool managed = request.wire_api == service::ProviderWireApi::kManaged;
  if (request.probe && (managed || !request.task_id.empty())) {
    // The probe relaxes exactly two rules and adds these two back as pins: a
    // probe is task-less by construction, and it speaks a person's own
    // endpoint, never the managed route — a probe of the product's own
    // service would spend the person's plan to test the product.
    Refuse(*effect, std::move(callback), service::EffectStatus::kDenied);
    return;
  }
  if (managed) {
    // Three facts the sandboxed core cannot be trusted to state, each checked
    // against this process's own knowledge (decision 0082). The endpoint
    // must be exactly the compiled worker origin — the core learned the host
    // from an entitlement summary, and the two independently sourced copies
    // agreeing is the check — and no stored-credential handle may ride a
    // managed call: its bearer is minted, never resolved from the store, so
    // a handle here is an effect nobody could have composed honestly.
    //
    // The third is the kind. The managed route is a catalog origin and never a
    // person's own address, and saying so here keeps that true independently
    // of the register: a register entry naming the worker origin cannot exist
    // — the command factory refuses the managed wire to a custom provider, and
    // so does the core's provider plane — but "cannot exist" is a fact about
    // two other files, and this one is where the request is sent.
    if (request.endpoint_kind != service::ModelEndpointKind::kCatalogOrigin ||
        !IsManagedOriginEndpoint(managed_worker_origin_, request.endpoint) ||
        request.credential_handle || request.not_before_monotonic_ms != 0u) {
      Refuse(*effect, std::move(callback), service::EffectStatus::kDenied);
      return;
    }
  }
  const std::optional<GURL> url =
      ResolveUrl(request.endpoint_kind, request.endpoint, *route);
  if (!url) {
    Refuse(*effect, std::move(callback), service::EffectStatus::kDenied);
    return;
  }

  // Resolved before the credential is asked for, and that order is the point:
  // a refusal must not have spent a trip to the secret store, and a secret
  // must not be sitting in this process while a route is being worked out.

  if (effect->effect_id.empty() || in_flight_.contains(effect->effect_id)) {
    // Two calls under one effect id would leave the map naming one of them,
    // so a cancellation would reach one and the other would answer into a
    // callback that had already been claimed.
    Refuse(*effect, std::move(callback), service::EffectStatus::kInvalidResult);
    return;
  }

  const uint64_t now = NowMonotonicMillisForModel();
  if (effect->operation->deadline_monotonic_ms <= now) {
    // Refused before anything leaves, rather than sent with a zero timeout: a
    // request that goes out past its deadline can still be billed.
    Refuse(*effect, std::move(callback),
           service::EffectStatus::kDeadlineExceeded);
    return;
  }
  if (request.not_before_monotonic_ms >=
          effect->operation->deadline_monotonic_ms &&
      request.not_before_monotonic_ms != 0u) {
    Refuse(*effect, std::move(callback),
           service::EffectStatus::kDeadlineExceeded);
    return;
  }

  const std::string effect_id = effect->effect_id;
  auto pending = std::make_unique<PendingCall>();
  pending->url = *url;
  pending->route = *route;
  pending->managed = managed;
  pending->callback = std::move(callback);
  pending->effect = std::move(effect);
  const uint64_t not_before =
      pending->effect->model_request->not_before_monotonic_ms;
  in_flight_.emplace(effect_id, std::move(pending));
  if (not_before > now) {
    base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE,
        base::BindOnce(&ProfileModelBroker::BeginRequestPreparation,
                       weak_factory_.GetWeakPtr(), effect_id),
        base::Milliseconds(static_cast<int64_t>(std::min<uint64_t>(
            not_before - now, std::numeric_limits<int64_t>::max()))));
    return;
  }
  BeginRequestPreparation(effect_id);
}

void ProfileModelBroker::BeginRequestPreparation(const std::string& effect_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto it = in_flight_.find(effect_id);
  if (it == in_flight_.end()) {
    return;
  }
  const PendingCall& entry = *it->second;
  if (entry.effect->operation->deadline_monotonic_ms <=
      NowMonotonicMillisForModel()) {
    Finish(effect_id,
           MakeResult(*entry.effect,
                      service::EffectStatus::kDeadlineExceeded, {}));
    return;
  }
  const service::ModelRequestEffect& request = *entry.effect->model_request;
  const std::optional<std::string> credential_handle =
      request.credential_handle;
  const std::string provider_id = request.provider_id;
  const bool managed = entry.managed;

  if (managed) {
    if (!entitlement_token_provider_) {
      // Unavailable rather than denied, for the credential resolver's
      // reason: what is missing is the browser's own seam, not the person's
      // entitlement.
      Finish(effect_id, MakeResult(*entry.effect,
                                   service::EffectStatus::kUnavailable, {}));
      return;
    }
    entitlement_token_provider_.Run(
        /*evict=*/false,
        mojo::WrapCallbackWithDefaultInvokeIfNotRun(
            base::BindOnce(&ProfileModelBroker::OnEntitlementToken,
                           weak_factory_.GetWeakPtr(), effect_id),
            std::nullopt));
    return;
  }

  if (!credential_handle) {
    // No handle is not a defect and not an omission. A provider a person runs
    // themselves — a local server speaking the completions dialect — has no
    // secret to send, and inventing a refusal here would make the one provider
    // that needs no credential the one provider that cannot be reached.
    StartRequest(effect_id, std::nullopt);
    return;
  }
  if (entry.effect->model_request->probe && *credential_handle != provider_id) {
    // A pasted draft's one-shot transient (decision 0083). The durable store
    // files a provider's stored key under the provider id itself, so a handle
    // that is anything else names a transient the browser minted for exactly
    // this send — spent here, once, and never entering durable records.
    if (!transient_credential_consumer_) {
      Finish(effect_id, MakeResult(*entry.effect,
                                   service::EffectStatus::kUnavailable, {}));
      return;
    }
    transient_credential_consumer_.Run(
        *credential_handle,
        mojo::WrapCallbackWithDefaultInvokeIfNotRun(
            base::BindOnce(&ProfileModelBroker::OnTransientCredential,
                           weak_factory_.GetWeakPtr(), effect_id),
            std::nullopt));
    return;
  }
  if (!credential_resolver_) {
    // Unavailable rather than denied. The person may well have configured this
    // provider; what is missing is the browser's way of asking, and saying
    // "denied" would put the absence of a seam on their account.
    Finish(effect_id,
           MakeResult(*entry.effect, service::EffectStatus::kUnavailable, {}));
    return;
  }

  // Phase one. The wrapper is what makes "exactly once" hold even if the
  // resolver is destroyed with the question outstanding — a dropped answer
  // becomes a refusal instead of a call the core waits on for the life of the
  // profile.
  credential_resolver_.Run(
      provider_id, *credential_handle,
      mojo::WrapCallbackWithDefaultInvokeIfNotRun(
          base::BindOnce(&ProfileModelBroker::OnCredentialResolved,
                         weak_factory_.GetWeakPtr(), effect_id),
          std::nullopt, std::nullopt));
}

void ProfileModelBroker::OnTransientCredential(
    std::string effect_id,
    std::optional<std::string> credential) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto it = in_flight_.find(effect_id);
  if (it == in_flight_.end()) {
    return;
  }
  if (!credential || credential->empty()) {
    // Absent or already spent: the draft never became a request. Unavailable
    // rather than denied, so the verdict honestly reads "never reached" and
    // not "the key is wrong" — nothing was judged.
    Finish(effect_id, MakeResult(*it->second->effect,
                                 service::EffectStatus::kUnavailable, {}));
    return;
  }
  StartRequest(effect_id, std::move(credential));
}

void ProfileModelBroker::OnEntitlementToken(std::string effect_id,
                                            std::optional<std::string> token) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto it = in_flight_.find(effect_id);
  if (it == in_flight_.end()) {
    return;
  }
  if (!token || token->empty()) {
    // The mint could not produce a token — no reachable worker, no usable
    // session, or a definitive absence the router will hear about through
    // the entitlement refresh. Nothing left this process and nothing was
    // billed.
    Finish(effect_id, MakeResult(*it->second->effect,
                                 service::EffectStatus::kUnavailable, {}));
    return;
  }
  StartRequest(effect_id, std::move(token));
}

service::EffectResultPtr ProfileModelBroker::MakeResult(
    const service::EffectEnvelope& effect,
    service::EffectStatus status,
    std::vector<uint8_t> completion,
    uint32_t provider_http_status,
    bool streamed,
    std::optional<service::ModelErrorClass> error_class,
    std::optional<uint64_t> retry_after_millis) const {
  auto result = service::EffectResult::New();
  result->operation = effect.operation.Clone();
  result->effect_id = effect.effect_id;
  result->kind = service::EffectKind::kModelRequest;
  result->status = status;
  result->model = service::ModelEffectResult::New();
  if (effect.model_request) {
    result->model->model_id = effect.model_request->model_id;
  }
  result->model->completion = std::move(completion);
  result->model->streamed = streamed;
  // Zero when the provider was never reached. The probe classifier reads
  // this fact rather than the coarsened status; a task turn ignores it.
  result->model->provider_http_status = provider_http_status;
  if (error_class) {
    result->model->failure = service::ModelFailure::New();
    result->model->failure->error_class = *error_class;
    result->model->failure->has_retry_after = retry_after_millis.has_value();
    result->model->failure->retry_after_millis =
        retry_after_millis.value_or(0u);
  }
  // `input_units` and `output_units` stay zero. They are read out of the reply
  // body, in a different place in each of the four families, by the decoder in
  // the sandbox that owns the family tables. Filling them here would mean this
  // process parsing a provider's JSON, and a guess written into a usage field
  // is a number a spend ledger would treat as measured.
  return result;
}

void ProfileModelBroker::Finish(const std::string& effect_id,
                                service::EffectResultPtr result) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto it = in_flight_.find(effect_id);
  if (it == in_flight_.end()) {
    return;
  }
  std::unique_ptr<PendingCall> pending = std::move(it->second);
  in_flight_.erase(it);
  // The loader is released before the callback runs, so a request that is
  // still open is aborted by this answer rather than after it.
  pending->loader.reset();
  std::move(pending->callback).Run(std::move(result));
}

void ProfileModelBroker::Refuse(const service::EffectEnvelope& effect,
                                EffectCallback callback,
                                service::EffectStatus status) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  std::move(callback).Run(MakeResult(effect, status, {}));
}

}  // namespace taffy

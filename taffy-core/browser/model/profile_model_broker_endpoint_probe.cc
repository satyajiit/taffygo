// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The browser leg of the endpoint probe (decision 0096 section 5).
//
// The core cannot ask an address what it is: it holds no socket, and the whole
// point of the probe is that nothing has been established about the server yet.
// So the question leaves the sandbox as `EffectKind::PROBE_CUSTOM_ENDPOINT`,
// this file resolves the credential and runs the three questions in
// custom_endpoint_prober.cc, and one typed verdict returns.
//
// Three properties hold here and none of them is incidental.
//
// **The address is judged by the register's rule, not the catalog's.** A probe
// is asked before a provider exists, so there is no registered row to recognize
// it against; what applies is `ClassifyCustomProviderEndpoint`, the same rule
// the command factory applied when it accepted the request. It is applied again
// here because this is where the request is actually sent, and a class that is
// safe only because of who calls it is safe until somebody else calls it.
//
// **Nothing is rewritten.** The verdict proposes a base; it never substitutes
// one. The surface shows the proposal and the person accepts it, which is what
// keeps decision 0096 section 1's "did this person type this?" answerable.
//
// **Nothing here logs.** The address is a fact about a person's own network and
// the credential is a credential — the model broker's rule, unchanged.

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "mojo/public/cpp/bindings/callback_helpers.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "taffy/browser/model/custom_provider_endpoint_policy.h"
#include "taffy/browser/model/profile_model_broker.h"
#include "url/gurl.h"

namespace taffy {
namespace {

namespace service = core_service::mojom;

// The models the listing named, as contract records.
//
// The display name is the identity, and deliberately: an OpenAI-shaped listing
// need not carry a second name. Limits and capabilities travel exactly as the
// bounded reader found them; discarding them here would save a reachable
// server whose models the task router can never use. Unknown facts retain the
// reader's zero/false values rather than gaining a capability during
// projection.
std::vector<service::CustomModelSpecPtr> ProbedModels(
    const std::vector<CustomModelReading>& readings) {
  std::vector<service::CustomModelSpecPtr> models;
  models.reserve(readings.size());
  for (const CustomModelReading& reading : readings) {
    models.push_back(service::CustomModelSpec::New(
        reading.model_id, reading.model_id, reading.context_window,
        reading.max_output_tokens, reading.reasoning, reading.tool_calling));
  }
  return models;
}

// The base the OpenAI-shaped API was proved at, or nothing.
//
// The proved-base arm returns the endpoint **off the effect** rather than a
// GURL's spelling of it. Those are the bytes the register will hold if the
// person saves, and a proposal that differed from them by a canonicalization
// would be an address they never typed being offered back to them as the one
// they did.
std::optional<std::string> ProvedBaseFor(
    const service::CustomEndpointProbeEffect& probe,
    const CustomEndpointProber::Answer& answer) {
  switch (answer.proved_base) {
    case CustomEndpointProber::ProvedBase::kNothing:
      return std::nullopt;
    case CustomEndpointProber::ProvedBase::kProbedBase:
      return probe.endpoint;
    case CustomEndpointProber::ProvedBase::kOriginV1: {
      const GURL base =
          CustomEndpointProber::OpenAiBaseAtOrigin(GURL(probe.endpoint));
      if (!base.is_valid() ||
          base.spec().size() > service::kMaxProviderEndpointBytes) {
        return std::nullopt;
      }
      return base.spec();
    }
  }
  return std::nullopt;
}

}  // namespace

ProfileModelBroker::PendingEndpointProbe::PendingEndpointProbe() = default;
ProfileModelBroker::PendingEndpointProbe::~PendingEndpointProbe() = default;

void ProfileModelBroker::DispatchEndpointProbe(
    service::EffectEnvelopePtr effect,
    EffectCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!effect || !effect->operation || !effect->custom_endpoint_probe ||
      effect->effect_id.empty()) {
    // A body this class cannot read at all. The caller still holds the
    // envelope and makes the terminal from it, which is a better answer than
    // one synthesized from a record missing the fields it would need.
    std::move(callback).Run(nullptr);
    return;
  }

  const service::CustomEndpointProbeEffect& probe =
      *effect->custom_endpoint_probe;
  const std::string effect_id = effect->effect_id;
  auto pending = std::make_unique<PendingEndpointProbe>();
  pending->callback = std::move(callback);

  // A second probe while one is open. Answered rather than queued, and
  // unavailable rather than denied: nothing about the address was judged.
  if (endpoint_probe_) {
    std::move(pending->callback)
        .Run(MakeEndpointProbeResult(
            *effect, service::EffectStatus::kUnavailable, nullptr));
    return;
  }

  // Checked again even though the command factory checked it when it accepted
  // the request, and asked here because this is where the request leaves. It
  // is the register rule (decision 0096 section 3) rather than the catalog
  // one: ports and base paths are the person's, and plain http reaches a
  // literal local machine and nothing else.
  if (!IsValidCustomEndpointProbe(probe) || !url_loader_factory_ ||
      ClassifyCustomProviderEndpoint(probe.endpoint) !=
          CustomEndpointRefusal::kNone ||
      // The managed wire is the product's own envelope spoken to its compiled
      // worker origin, and the other two are vendors' subscription endpoints.
      // None of them is an address a person runs, so a probe claiming one is
      // refused here as well as in the core's provider plane. This is a chain
      // of comparisons and not a switch, so nothing fails to compile when a
      // family is added — the list has to be renewed by hand.
      probe.wire_api == service::ProviderWireApi::kManaged ||
      probe.wire_api == service::ProviderWireApi::kOpenAiCodexResponses ||
      probe.wire_api == service::ProviderWireApi::kGoogleCloudCodeAssist) {
    std::move(pending->callback)
        .Run(MakeEndpointProbeResult(*effect, service::EffectStatus::kDenied,
                                     nullptr));
    return;
  }

  const std::optional<std::string> credential_handle = probe.credential_handle;
  const std::string provider_id = probe.provider_id;
  pending->effect = std::move(effect);
  endpoint_probe_ = std::move(pending);

  if (!credential_handle) {
    // No handle is the ordinary case for a server a person runs themselves,
    // not an omission: there is nothing to send, and inventing a refusal would
    // make the address this record exists for the one that cannot be checked.
    OnEndpointProbeCredential(effect_id, std::nullopt);
    return;
  }
  if (*credential_handle != provider_id) {
    // A pasted draft's one-shot transient (decision 0083). The durable store
    // files a provider's stored key under the provider id itself, so a handle
    // that is anything else names a transient the browser minted for exactly
    // this send.
    if (!transient_credential_consumer_) {
      FinishEndpointProbe(effect_id, service::EffectStatus::kUnavailable,
                          nullptr);
      return;
    }
    transient_credential_consumer_.Run(
        *credential_handle,
        mojo::WrapCallbackWithDefaultInvokeIfNotRun(
            base::BindOnce(&ProfileModelBroker::OnEndpointProbeCredential,
                           weak_factory_.GetWeakPtr(), effect_id),
            std::nullopt));
    return;
  }
  // Re-probing an address that is already saved: the handle names the
  // provider's own stored key, so it is resolved rather than spent.
  if (!credential_resolver_) {
    // Unavailable rather than denied. What is missing is the browser's way of
    // asking, and saying "denied" would put the absence of a seam on the
    // person's configuration.
    FinishEndpointProbe(effect_id, service::EffectStatus::kUnavailable,
                        nullptr);
    return;
  }
  credential_resolver_.Run(
      provider_id, *credential_handle,
      mojo::WrapCallbackWithDefaultInvokeIfNotRun(
          base::BindOnce(&ProfileModelBroker::OnEndpointProbeResolved,
                         weak_factory_.GetWeakPtr(), effect_id),
          std::nullopt, std::nullopt));
}

// The store's answer carries a second value a model call reads and this one
// must not: the origin the sealed record says that credential's requests
// belong to. A person's own registered address is never substituted (decision
// 0096 section 1) — replacing a host inside it would be this process answering
// the question the register exists to be the only answer to — so the origin is
// dropped here rather than applied.
void ProfileModelBroker::OnEndpointProbeResolved(
    std::string effect_id,
    std::optional<std::string> credential,
    std::optional<std::string> /*credential_origin*/) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  OnEndpointProbeCredential(std::move(effect_id), std::move(credential));
}

void ProfileModelBroker::OnEndpointProbeCredential(
    std::string effect_id,
    std::optional<std::string> credential) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!endpoint_probe_ || endpoint_probe_->effect->effect_id != effect_id) {
    return;
  }
  if (!endpoint_prober_) {
    endpoint_prober_ =
        std::make_unique<CustomEndpointProber>(url_loader_factory_);
  }
  const GURL endpoint(endpoint_probe_->effect->custom_endpoint_probe->endpoint);
  endpoint_prober_->Probe(
      endpoint, std::move(credential),
      base::BindOnce(&ProfileModelBroker::OnEndpointProbeAnswered,
                     weak_factory_.GetWeakPtr(), std::move(effect_id)));
}

void ProfileModelBroker::OnEndpointProbeAnswered(
    std::string effect_id,
    CustomEndpointProber::Answer answer) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  // Completed either way. An address that answered nowhere is a verdict this
  // browser reached, not a leg that failed to run, and `reached` is what says
  // which happened — the same division the listing fetch draws between its
  // effect status and its disposition.
  FinishEndpointProbe(effect_id, service::EffectStatus::kCompleted, &answer);
}

void ProfileModelBroker::FinishEndpointProbe(
    const std::string& effect_id,
    service::EffectStatus status,
    const CustomEndpointProber::Answer* answer) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!endpoint_probe_ || endpoint_probe_->effect->effect_id != effect_id) {
    return;
  }
  // Lifted out of the member before the callback runs, so a caller that probes
  // again from inside it sees a broker holding nothing.
  std::unique_ptr<PendingEndpointProbe> pending = std::move(endpoint_probe_);
  std::move(pending->callback)
      .Run(MakeEndpointProbeResult(*pending->effect, status, answer));
}

service::EffectResultPtr ProfileModelBroker::MakeEndpointProbeResult(
    const service::EffectEnvelope& effect,
    service::EffectStatus status,
    const CustomEndpointProber::Answer* answer) const {
  auto result = service::EffectResult::New();
  result->operation = effect.operation.Clone();
  result->effect_id = effect.effect_id;
  result->kind = service::EffectKind::kProbeCustomEndpoint;
  result->status = status;

  auto probe_result = service::CustomEndpointProbeResult::New();
  const service::CustomEndpointProbeEffect* probe =
      effect.custom_endpoint_probe.get();
  if (probe) {
    probe_result->provider_id = probe->provider_id;
  }
  const bool reached = answer && answer->reached;
  probe_result->reached = reached;
  if (reached) {
    // The runtime is stated only when one was reached, because
    // `kOpenaiCompatible` is the zero value of the contract's enumeration and
    // filing it for an address that answered nowhere would be a finding the
    // probe never made.
    probe_result->detected_server =
        service::DetectedServer::New(answer->server_kind);
    probe_result->model_count = answer->model_count;
    probe_result->models = ProbedModels(answer->models);
    if (probe) {
      probe_result->proved_base = ProvedBaseFor(*probe, *answer);
    }
  }
  result->custom_endpoint_probe = std::move(probe_result);
  return result;
}

}  // namespace taffy

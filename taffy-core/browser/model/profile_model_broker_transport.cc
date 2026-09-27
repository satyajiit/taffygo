// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The sending half of one model call: the body the core wrote, the headers
// this transport composes around it, and one loader started exactly once.
// What the loader reports back is classified in
// profile_model_broker_outcome.cc, which is where the transport's facts become
// the effect's terminal.

#include <stddef.h>
#include <stdint.h>

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "base/base64.h"
#include "base/containers/span.h"
#include "base/functional/bind.h"
#include "base/strings/strcat.h"
#include "base/time/time.h"
#include "mojo/public/cpp/bindings/callback_helpers.h"
#include "net/base/load_flags.h"
#include "net/http/http_request_headers.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "services/network/public/mojom/url_loader.mojom.h"
#include "services/network/public/mojom/url_response_head.mojom.h"
#include "taffy/browser/model/profile_model_broker.h"
#include "taffy/browser/model/profile_model_broker_route.h"
#include "taffy/browser/profile_page_media_store.h"
#include "taffy/browser/providerauth/provider_auth_claims.h"

namespace taffy {
namespace {

namespace service = core_service::mojom;

void ClearString(std::string* value) {
  if (!value) {
    return;
  }
  std::ranges::fill(*value, '\0');
  value->clear();
}

uint64_t NowMonotonicMillisForTransport() {
  const int64_t value = base::TimeTicks::Now().since_origin().InMilliseconds();
  return value < 0 ? 0u : static_cast<uint64_t>(value);
}

}  // namespace

void ProfileModelBroker::StartRequest(const std::string& effect_id,
                                      std::optional<std::string> credential) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto it = in_flight_.find(effect_id);
  if (it == in_flight_.end()) {
    if (credential) {
      ClearString(&*credential);
    }
    return;
  }
  PendingCall& pending = *it->second;
  const service::ModelRequestEffect& request = *pending.effect->model_request;
  const bool stream_task_answer = !request.task_id.empty();

  const uint64_t now = NowMonotonicMillisForTransport();
  if (pending.effect->operation->deadline_monotonic_ms <= now) {
    // Phase one can take long enough for the deadline to pass — a vault that
    // needed a device unlock is the ordinary case. Nothing has left yet, so
    // this is a deadline rather than an unknown outcome.
    if (credential) {
      ClearString(&*credential);
    }
    Finish(effect_id, MakeResult(*pending.effect,
                                 service::EffectStatus::kDeadlineExceeded, {}));
    return;
  }
  if (!url_loader_factory_) {
    if (credential) {
      ClearString(&*credential);
    }
    Finish(effect_id, MakeResult(*pending.effect,
                                 service::EffectStatus::kUnavailable, {}));
    return;
  }
  if (stream_task_answer && !model_stream_chunk_dispatcher_) {
    if (credential) {
      ClearString(&*credential);
    }
    Finish(effect_id, MakeResult(*pending.effect,
                                 service::EffectStatus::kUnavailable, {}));
    return;
  }

  std::string body(request.request_body.begin(), request.request_body.end());
  if (request.media_attachment_handle) {
    if (!page_media_store_) {
      if (credential) {
        ClearString(&*credential);
      }
      Finish(effect_id, MakeResult(*pending.effect,
                                   service::EffectStatus::kUnavailable, {}));
      return;
    }
    // Take first. A known handle is one-use even when the core supplied a
    // stale binding or a malformed body, so neither mistake can probe the
    // custody repeatedly. The store independently checks task, generation,
    // disclosure, MIME, expiry and the live document revision.
    std::optional<ProfilePageMediaStore::ResolvedAttachment> attachment =
        page_media_store_->TakeForModel(
            request.task_id, pending.effect->operation->service_generation,
            *request.media_attachment_handle,
            *request.media_attachment_mime_type, request.disclosure,
            base::TimeTicks::Now());
    if (!attachment) {
      if (credential) {
        ClearString(&*credential);
      }
      Finish(effect_id, MakeResult(*pending.effect,
                                   service::EffectStatus::kUnavailable, {}));
      return;
    }

    // The sandboxed family writer places the random handle exactly once where
    // that family expects base64 image data. Replace bytes, not JSON: this
    // trusted process never parses an untrusted provider document. A missing
    // or repeated handle means the body and typed attachment disagree and the
    // request is refused after the one-use claim has been burned.
    const std::string& marker = *request.media_attachment_handle;
    const size_t marker_at = body.find(marker);
    if (marker_at == std::string::npos ||
        body.find(marker, marker_at + marker.size()) != std::string::npos) {
      if (credential) {
        ClearString(&*credential);
      }
      Finish(effect_id, MakeResult(*pending.effect,
                                   service::EffectStatus::kInvalidResult, {}));
      return;
    }
    constexpr size_t kMaximumEncodedAttachmentBytes =
        4u *
        ((static_cast<size_t>(service::kMaxMediaAttachmentBytes) + 2u) / 3u);
    constexpr size_t kMaximumUploadBytes =
        static_cast<size_t>(service::kMaxEffectBytes) +
        kMaximumEncodedAttachmentBytes;
    const size_t encoded_size = 4u * ((attachment->bytes.size() + 2u) / 3u);
    if (encoded_size > kMaximumEncodedAttachmentBytes ||
        body.size() - marker.size() > kMaximumUploadBytes - encoded_size) {
      if (credential) {
        ClearString(&*credential);
      }
      Finish(effect_id, MakeResult(*pending.effect,
                                   service::EffectStatus::kResourceLimit, {}));
      return;
    }
    std::string expanded;
    expanded.reserve(body.size() - marker.size() + encoded_size);
    expanded.append(body, 0u, marker_at);
    base::Base64EncodeAppend(base::span(attachment->bytes), &expanded);
    expanded.append(body, marker_at + marker.size(), std::string::npos);
    body = std::move(expanded);
  }

  auto resource_request = std::make_unique<network::ResourceRequest>();
  resource_request->url = pending.url;
  resource_request->method = "POST";
  // No cache, in either direction. A model reply is a paid, one-off answer;
  // serving one from a cache would answer a question that was never asked and
  // storing one would leave a person's conversation on the disk in a place
  // nothing in this product knows how to clear.
  resource_request->load_flags =
      net::LOAD_DISABLE_CACHE | net::LOAD_BYPASS_CACHE;
  // No cookies out and none kept. The provider is being authenticated with a
  // credential the person configured, and an ambient cookie would be a second
  // identity travelling with a request that already has one.
  resource_request->credentials_mode = network::mojom::CredentialsMode::kOmit;
  // A redirect is refused rather than followed. Following one would send the
  // credential wherever the response pointed, which is a destination this
  // process checked nothing about and the person configured nothing about.
  resource_request->redirect_mode = network::mojom::RedirectMode::kError;

  // The core's headers first, then this transport's own, so that nothing the
  // core proposed can shadow the credential or the media type. The static
  // header list has already been refused any name a credential travels in and
  // any name this transport composes, so this ordering is the second of two
  // reasons rather than the only one.
  for (const service::ModelStaticHeaderPtr& header : request.static_headers) {
    resource_request->headers.SetHeader(header->name, header->value);
  }
  // The family's own fixed headers, written after the core's and before the
  // credential, so a name the family owns is this transport's whatever the
  // core proposed. Every name in the table is also on the list the core is
  // refused, so the overwrite is a second guard rather than the only one.
  for (const model_broker::FamilyHeader& header :
       pending.route.family_headers) {
    resource_request->headers.SetHeader(std::string(header.name),
                                        std::string(header.value));
  }
  resource_request->headers.SetHeader(net::HttpRequestHeaders::kAccept,
                                      "application/json");
  if (credential) {
    const model_broker::CredentialPlacement& placement =
        pending.route.credential;
    resource_request->headers.SetHeader(
        std::string(placement.header_name),
        base::StrCat({placement.value_prefix, *credential}));
    // Read from the credential while it is still here, and only where the
    // family asks for it. A claim that cannot be read is simply not sent: the
    // vendor answers a request with no account named the way it chooses to,
    // and inventing a value would name an account nobody has.
    const model_broker::CredentialClaimHeader& claim =
        pending.route.credential_claim;
    if (!claim.header_name.empty()) {
      const std::optional<std::string> value = provider_auth::ReadStringClaim(
          *credential, claim.claim_namespace, claim.claim_key);
      if (value) {
        resource_request->headers.SetHeader(std::string(claim.header_name),
                                            *value);
      }
    }
    ClearString(&*credential);
  }

  constexpr net::NetworkTrafficAnnotationTag kTrafficAnnotation =
      net::DefineNetworkTrafficAnnotation("taffy_model_broker", R"(
        semantics {
          sender: "TaffyGo model broker"
          description: "Sends one assistant request to the model provider the "
                       "person configured, and returns the reply."
          trigger: "Taffy takes a turn in a task the person started, or the "
                   "person asks Taffy something directly."
          data: "The request the isolated core composed: the conversation and "
                "any page text the person chose to include, plus the model "
                "name and the answer allowance. The provider credential "
                "travels in one header."
          destination: OTHER
          internal { contacts { owners: "//taffy/OWNERS" } }
          user_data {
            type: USER_CONTENT
            type: WEB_CONTENT
            type: ACCESS_TOKEN
          }
          last_reviewed: "2026-08-26"
        }
        policy {
          cookies_allowed: NO
          setting: "The person chooses the provider, supplies its credential, "
                   "and can remove both from settings. With none configured, "
                   "no request is made."
          policy_exception_justification: "Not implemented."
        })");

  auto loader = network::SimpleURLLoader::Create(std::move(resource_request),
                                                 kTrafficAnnotation);
  // Preserve HTTP responses for both protocols. A task-less one-shot carries
  // an error body to its dedicated decoder; the streamed task path below
  // suppresses it from answer chunks and settles from the typed status.
  loader->SetAllowHttpErrorResults(true);
  // No transport retry, ever. The browser journals an effect identity before
  // dispatching and refuses one it has already journalled, so a retry taken
  // here would be a second paid call under one journalled identity — spent,
  // and invisible to the ledger that exists to say what was spent. Retrying is
  // the core's decision, and it makes it by proposing another effect.
  loader->SetRetryOptions(0, network::SimpleURLLoader::RETRY_NEVER);
  loader->SetTimeoutDuration(base::Milliseconds(
      pending.effect->operation->deadline_monotonic_ms - now));
  loader->AttachStringForUpload(std::move(body), pending.route.content_type);

  network::SimpleURLLoader* raw_loader = loader.get();
  pending.loader = std::move(loader);
  // From here the request may cost money, which is what `CancelTask` reads the
  // loader's presence to know.
  //
  if (stream_task_answer) {
    // Headers arrive before body data and decide whether any bytes are allowed
    // to become answer text. In particular, a managed 401/402 body belongs to
    // the refused token attempt and must not leak into the fresh-token retry.
    raw_loader->SetOnResponseStartedCallback(
        base::BindOnce(&ProfileModelBroker::OnModelResponseStarted,
                       weak_factory_.GetWeakPtr(), effect_id));
    pending.stream_consumer = std::make_unique<ModelStreamConsumer>(
        weak_factory_.GetWeakPtr(), effect_id);
    raw_loader->DownloadAsStream(url_loader_factory_.get(),
                                 pending.stream_consumer.get());
    return;
  }

  // Task-less suggestions and key probes have distinct terminal protocols and
  // remain bounded one-shot replies. The task path above is the only path with
  // a retained turn and therefore the only one the incremental Rust decoder
  // may accept.
  raw_loader->DownloadToString(
      url_loader_factory_.get(),
      mojo::WrapCallbackWithDefaultInvokeIfNotRun(
          base::BindOnce(&ProfileModelBroker::OnResponse,
                         weak_factory_.GetWeakPtr(), effect_id),
          std::nullopt),
      request.max_output_bytes);
}

}  // namespace taffy

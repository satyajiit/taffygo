// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The answering half of one model call: what the loader reports when it
// completes, classified into the effect's terminal. Both protocols' loader
// completions land here — the streamed task answer through
// `OnModelStreamComplete` and the bounded one-shot reply through `OnResponse`
// — and both read the same two facts, the network error and the HTTP status,
// through the same classification. A stream this browser abandons itself is
// settled in profile_model_broker_stream.cc and reads no loader error. The
// request is composed and started in profile_model_broker_transport.cc.

#include <stddef.h>
#include <stdint.h>

#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/strings/string_number_conversions.h"
#include "mojo/public/cpp/bindings/callback_helpers.h"
#include "net/base/net_errors.h"
#include "net/http/http_response_headers.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "services/network/public/mojom/url_response_head.mojom.h"
#include "taffy/browser/model/profile_model_broker.h"

namespace taffy {
namespace {

namespace service = core_service::mojom;

struct TransportOutcome {
  service::EffectStatus status;
  std::optional<service::ModelErrorClass> error_class;
};

// The browser classifies observed transport facts; it does not decide retry.
// Any network error after the loader exists is outcome-unknown, because the
// request may already have reached a paid provider.
TransportOutcome OutcomeFor(int net_error,
                            int http_status,
                            bool typed_direct_failure) {
  if (net_error != net::OK) {
    // The one line this subsystem writes (decision 0217). Both completion
    // paths classify their error here, and until this line nothing wrote it
    // down, so whether an unknown outcome could have reached a provider was
    // unanswerable from a device log. A compiled-in error name and a status
    // code; nothing a request or a reply carried.
    //
    // `ErrorToShortString` only for a value net_error_list.h defines: for any
    // other it reaches `DUMP_WILL_BE_NOTREACHED`, fatal in every build this
    // product makes, and the value came over a pipe from the network service.
    // A code this build does not know is written as its number.
    LOG(WARNING) << "[taffy_model_transport_error] net="
                 << (net::IsOkOrDefinedError(net_error)
                         ? net::ErrorToShortString(net_error)
                         : base::NumberToString(net_error))
                 << " http=" << http_status;
  }
  if (net_error == net::ERR_INSUFFICIENT_RESOURCES) {
    return {service::EffectStatus::kResourceLimit,
            typed_direct_failure
                ? std::optional(service::ModelErrorClass::kOverflow)
                : std::nullopt};
  }
  if (net_error != net::OK) {
    return {service::EffectStatus::kOutcomeUnknown, std::nullopt};
  }
  if (http_status >= 200 && http_status < 300) {
    return {service::EffectStatus::kCompleted, std::nullopt};
  }
  service::ModelErrorClass error_class =
      service::ModelErrorClass::kInvalidRequest;
  service::EffectStatus status = service::EffectStatus::kInvalidResult;
  if (http_status == 401 || http_status == 403) {
    error_class = service::ModelErrorClass::kAuth;
    status = service::EffectStatus::kDenied;
  } else if (http_status == 402 || http_status == 429) {
    error_class = service::ModelErrorClass::kQuota;
    status = service::EffectStatus::kDenied;
  } else if (http_status == 408) {
    error_class = service::ModelErrorClass::kNetwork;
    status = service::EffectStatus::kUnavailable;
  } else if (http_status >= 500) {
    error_class = service::ModelErrorClass::kOverloaded;
    status = service::EffectStatus::kUnavailable;
  }
  return {status,
          typed_direct_failure ? std::optional(error_class) : std::nullopt};
}

std::optional<uint64_t> RetryAfterMillis(
    const network::mojom::URLResponseHead* head) {
  if (!head || !head->headers) {
    return std::nullopt;
  }
  // `EnumerateHeader`, not `GetNormalizedHeader`, and not as a matter of
  // taste: `retry-after` is on Chromium's non-coalescing list
  // (`HttpUtil::IsNonCoalescingHeader`), and the normalising accessor
  // `DCHECK`s on every name in that list — "please use EnumerateHeader
  // instead", as its own comment puts it. That made this line fatal in a
  // DCHECK build on the one path a vendor takes most: a 429. In a build with
  // DCHECKs off it was quieter and no better, joining a repeated header into
  // "30, 60" and reading a number out of neither.
  //
  // The first value is the answer. A second `Retry-After` is a server
  // contradicting itself, and this takes what it said first rather than
  // summing, concatenating, or preferring the larger.
  size_t iter = 0;
  const std::optional<std::string_view> value =
      head->headers->EnumerateHeader(&iter, "retry-after");
  uint64_t seconds = 0;
  if (!value || !base::StringToUint64(*value, &seconds)) {
    return std::nullopt;
  }
  constexpr uint64_t kMillisPerSecond = 1000u;
  return seconds > std::numeric_limits<uint64_t>::max() / kMillisPerSecond
             ? std::numeric_limits<uint64_t>::max()
             : seconds * kMillisPerSecond;
}

}  // namespace

void ProfileModelBroker::OnModelStreamComplete(std::string effect_id,
                                               bool success) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto it = in_flight_.find(effect_id);
  if (it == in_flight_.end()) {
    return;
  }
  PendingCall& pending = *it->second;
  if (!pending.loader || pending.stream_resume ||
      !pending.stream_piece.empty() || pending.stream_abort_scheduled) {
    return;
  }

  int net_error = pending.loader->NetError();
  if (!success && net_error == net::OK) {
    net_error = net::ERR_FAILED;
  }
  int http_status = pending.response_http_status;
  if (http_status == 0) {
    const network::mojom::URLResponseHead* head =
        pending.loader->ResponseInfo();
    if (head && head->headers) {
      http_status = head->headers->response_code();
    }
  }

  if (pending.managed && !pending.managed_retry_used &&
      pending.stream_sequence == 0u && net_error == net::OK &&
      (http_status == 401 || http_status == 402)) {
    pending.managed_retry_used = true;
    pending.stream_consumer.reset();
    pending.loader.reset();
    pending.response_started = false;
    pending.response_http_status = 0;
    pending.streamed_response_bytes = 0u;
    if (entitlement_token_provider_) {
      entitlement_token_provider_.Run(
          /*evict=*/true,
          mojo::WrapCallbackWithDefaultInvokeIfNotRun(
              base::BindOnce(&ProfileModelBroker::OnEntitlementToken,
                             weak_factory_.GetWeakPtr(), effect_id),
              std::nullopt));
      return;
    }
  }
  if (pending.managed && net_error == net::OK && http_status == 402 &&
      quota_refused_callback_) {
    quota_refused_callback_.Run();
  }

  const bool typed_direct_failure =
      !pending.managed && !pending.effect->model_request->task_id.empty() &&
      pending.stream_sequence == 0u;
  const TransportOutcome outcome =
      OutcomeFor(net_error, http_status, typed_direct_failure);
  const network::mojom::URLResponseHead* response_head =
      pending.loader->ResponseInfo();

  Finish(effect_id,
         MakeResult(*pending.effect, outcome.status, {},
                    http_status < 0 ? 0u : static_cast<uint32_t>(http_status),
                    pending.stream_sequence != 0u, outcome.error_class,
                    outcome.error_class ? RetryAfterMillis(response_head)
                                        : std::nullopt));
}

void ProfileModelBroker::OnResponse(std::string effect_id,
                                    std::optional<std::string> response_body) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto it = in_flight_.find(effect_id);
  if (it == in_flight_.end()) {
    return;
  }
  PendingCall& pending = *it->second;
  if (!pending.loader) {
    // The default invocation for a loader that was never started. Nothing to
    // report about a request that does not exist.
    return;
  }

  const int net_error = pending.loader->NetError();
  int http_status = 0;
  const network::mojom::URLResponseHead* head = pending.loader->ResponseInfo();
  if (head && head->headers) {
    http_status = head->headers->response_code();
  }

  if (pending.managed && !pending.managed_retry_used && net_error == net::OK &&
      (http_status == 401 || http_status == 402)) {
    // One evict-and-remint retry (decision 0082). A 401 can be a token that
    // lapsed between mint and dispatch, and a 402 can be an entitlement that
    // renewed since the summary the token was minted beside — a top-up, a
    // window turn. A fresh token answers both without a person doing
    // anything, and it is free in the one sense that matters: the worker
    // refused before serving, so nothing was billed and the exactly-once
    // claim on this request's identity is not yet settled. The second
    // refusal is the worker's settled answer and maps below.
    pending.managed_retry_used = true;
    pending.loader.reset();
    if (entitlement_token_provider_) {
      entitlement_token_provider_.Run(
          /*evict=*/true,
          mojo::WrapCallbackWithDefaultInvokeIfNotRun(
              base::BindOnce(&ProfileModelBroker::OnEntitlementToken,
                             weak_factory_.GetWeakPtr(), effect_id),
              std::nullopt));
      return;
    }
  }
  if (pending.managed && net_error == net::OK && http_status == 402 &&
      quota_refused_callback_) {
    // The settled quota refusal is also the freshest fact there is about the
    // entitlement; this poke is how the core hears it and re-asks, on the
    // refresh protocol's own rate gate.
    quota_refused_callback_.Run();
  }

  std::vector<uint8_t> completion;
  if (response_body) {
    completion.assign(response_body->begin(), response_body->end());
  }
  const bool typed_direct_failure =
      !pending.managed && !pending.effect->model_request->task_id.empty();
  const TransportOutcome outcome =
      OutcomeFor(net_error, http_status, typed_direct_failure);
  Finish(effect_id,
         MakeResult(*pending.effect, outcome.status,
                    std::move(completion),
                    http_status < 0 ? 0u : static_cast<uint32_t>(http_status),
                    false, outcome.error_class,
                    outcome.error_class ? RetryAfterMillis(head)
                                        : std::nullopt));
}

}  // namespace taffy

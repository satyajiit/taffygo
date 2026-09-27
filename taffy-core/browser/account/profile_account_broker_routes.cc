// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/account/profile_account_broker.h"

#include <algorithm>
#include <ranges>
#include <string_view>
#include <utility>

#include "base/json/json_writer.h"
#include "base/time/time.h"
#include "base/values.h"
#include "net/base/net_errors.h"
#include "net/base/url_util.h"
#include "taffy/browser/account_plane_configuration.h"
#include "taffy/browser/core_account_effect_terminal.h"

namespace taffy {
namespace {

namespace service = core_service::mojom;

constexpr char kProductAuthCallback[] = "com.taffygo.browser://auth";

uint64_t NowMonotonicMillisForRoutes() {
  const int64_t value = base::TimeTicks::Now().since_origin().InMilliseconds();
  return value < 0 ? 0u : static_cast<uint64_t>(value);
}

bool IsOpaqueIdentifier(std::string_view value) {
  if (value.empty() || value.size() > service::kMaxIdentifierBytes) {
    return false;
  }
  for (const char character : value) {
    if (!((character >= 'A' && character <= 'Z') ||
          (character >= 'a' && character <= 'z') ||
          (character >= '0' && character <= '9') || character == '-' ||
          character == '_')) {
      return false;
    }
  }
  return true;
}

bool IsPkceS256Challenge(std::string_view value) {
  constexpr size_t kPkceS256ChallengeBytes = 43u;
  return value.size() == kPkceS256ChallengeBytes &&
         IsOpaqueIdentifier(value);
}

}  // namespace

void ProfileAccountBroker::OnCurrentSessionReadForRevoke(
    service::EffectEnvelopePtr effect,
    EffectCallback callback,
    service::EffectStatus status,
    browser::account::mojom::SessionMaterialReadResultPtr stored) {
  if (status != service::EffectStatus::kCompleted || !stored) {
    if (stored) {
      std::ranges::fill(stored->material, 0u);
      stored->material.clear();
    }
    DeleteSessionAndFinish(std::move(effect), std::move(callback), status);
    return;
  }
  std::optional<SessionPayload> session = DecodeSessionPayload(stored->material);
  std::ranges::fill(stored->material, 0u);
  stored->material.clear();
  if (!session) {
    DeleteSessionAndFinish(std::move(effect), std::move(callback),
                           service::EffectStatus::kInvalidResult);
    return;
  }
  const std::optional<GURL> endpoint =
      AccountEndpoint(AccountEndpointKind::kLogout);
  if (!endpoint || effect->operation->deadline_monotonic_ms <=
                       NowMonotonicMillisForRoutes()) {
    DeleteSessionAndFinish(
        std::move(effect), std::move(callback),
        endpoint ? service::EffectStatus::kDeadlineExceeded
                 : service::EffectStatus::kUnavailable);
    return;
  }
  StartJsonRequest(
      *effect, *endpoint, "{}", session->access_token,
      base::BindOnce(&ProfileAccountBroker::OnRevokeResponse,
                     weak_factory_.GetWeakPtr(), std::move(effect),
                     std::move(callback)));
}

void ProfileAccountBroker::OnRevokeResponse(
    service::EffectEnvelopePtr effect,
    EffectCallback callback,
    NetworkResponse response) {
  if (!response.dispatched) {
    if (response.body) {
      std::ranges::fill(*response.body, '\0');
      response.body->clear();
    }
    const service::EffectStatus status =
        response.net_error == net::ERR_TIMED_OUT
            ? service::EffectStatus::kDeadlineExceeded
            : service::EffectStatus::kInvalidResult;
    DeleteSessionAndFinish(std::move(effect), std::move(callback), status);
    return;
  }
  const AccountNetworkResponseDisposition disposition =
      ClassifyAccountNetworkResponse(response.net_error, response.http_status,
                                     response.body.has_value());
  if (disposition == AccountNetworkResponseDisposition::kRejected &&
      response.body) {
    LogAccountRefusal("logout", response.http_status, *response.body);
  }
  if (response.body) {
    std::ranges::fill(*response.body, '\0');
    response.body->clear();
  }
  service::EffectStatus status = service::EffectStatus::kOutcomeUnknown;
  if (disposition == AccountNetworkResponseDisposition::kCompleted) {
    status = service::EffectStatus::kCompleted;
  } else if (disposition == AccountNetworkResponseDisposition::kRejected) {
    status = service::EffectStatus::kDenied;
  }
  DeleteSessionAndFinish(std::move(effect), std::move(callback), status);
}

void ProfileAccountBroker::StartEmailLinkRequest(
    service::EffectEnvelopePtr effect,
    EffectCallback callback) {
  const auto& request = effect->network_request->request_email_link;
  const uint64_t now = NowMonotonicMillisForRoutes();
  PruneExpiredBindings(now);
  if (!request || !IsOpaqueIdentifier(request->flow_id) ||
      !IsOpaqueIdentifier(request->redirect_binding_id) ||
      !IsOpaqueIdentifier(request->state) ||
      !IsOpaqueIdentifier(request->pkce_verifier_handle) ||
      !IsPkceS256Challenge(request->pkce_challenge) ||
      oauth_bindings_by_state_.contains(request->state) ||
      oauth_bindings_by_state_.size() >= service::kMaxPendingAccountFlows ||
      effect->operation->deadline_monotonic_ms <= now) {
    DeleteTransientHandlesForFailedEffect(*effect);
    std::move(callback).Run(
        MakeResult(*effect, service::EffectStatus::kDenied));
    return;
  }
  const std::optional<GURL> base_endpoint =
      AccountEndpoint(AccountEndpointKind::kEmailOtp);
  if (!base_endpoint) {
    DeleteTransientHandlesForFailedEffect(*effect);
    std::move(callback).Run(
        MakeResult(*effect, service::EffectStatus::kUnavailable));
    return;
  }
  GURL callback_url = net::AppendQueryParameter(
      GURL(kProductAuthCallback), "state", request->state);
  GURL endpoint = net::AppendQueryParameter(*base_endpoint, "redirect_to",
                                            callback_url.spec());
  if (!endpoint.is_valid()) {
    DeleteTransientHandlesForFailedEffect(*effect);
    std::move(callback).Run(
        MakeResult(*effect, service::EffectStatus::kInvalidResult));
    return;
  }
  base::DictValue body;
  body.Set("email", request->email);
  body.Set("create_user", true);
  body.Set("code_challenge", request->pkce_challenge);
  body.Set("code_challenge_method", "s256");
  std::string encoded;
  if (!base::JSONWriter::Write(body, &encoded)) {
    DeleteTransientHandlesForFailedEffect(*effect);
    std::move(callback).Run(
        MakeResult(*effect, service::EffectStatus::kInvalidResult));
    return;
  }
  oauth_bindings_by_state_.emplace(
      request->state,
      PendingOAuthBinding{request->flow_id, request->redirect_binding_id,
                          request->pkce_verifier_handle,
                          effect->operation->service_generation,
                          effect->operation->deadline_monotonic_ms});
  ScheduleBindingExpiry(now);
  StartJsonRequest(
      *effect, endpoint, std::move(encoded), std::nullopt,
      base::BindOnce(&ProfileAccountBroker::OnEmailLinkResponse,
                     weak_factory_.GetWeakPtr(), std::move(effect),
                     std::move(callback)));
}

void ProfileAccountBroker::OnEmailLinkResponse(
    service::EffectEnvelopePtr effect,
    EffectCallback callback,
    NetworkResponse response) {
  const std::string state =
      effect->network_request->request_email_link->state;
  AccountNetworkResponseDisposition disposition =
      AccountNetworkResponseDisposition::kOutcomeUnknown;
  service::EffectStatus pre_dispatch_status =
      service::EffectStatus::kInvalidResult;
  if (!response.dispatched) {
    pre_dispatch_status = response.net_error == net::ERR_TIMED_OUT
                              ? service::EffectStatus::kDeadlineExceeded
                              : service::EffectStatus::kInvalidResult;
  } else {
    disposition = ClassifyAccountNetworkResponse(
        response.net_error, response.http_status, response.body.has_value());
  }
  if (disposition == AccountNetworkResponseDisposition::kRejected &&
      response.body) {
    LogAccountRefusal("email link", response.http_status, *response.body);
  }
  if (response.body) {
    std::ranges::fill(*response.body, '\0');
    response.body->clear();
  }
  if (!response.dispatched) {
    auto binding = oauth_bindings_by_state_.find(state);
    if (binding != oauth_bindings_by_state_.end()) {
      DeleteTransientBestEffort(binding->second.pkce_verifier_handle);
      oauth_bindings_by_state_.erase(binding);
      ScheduleBindingExpiry(NowMonotonicMillisForRoutes());
    }
    std::move(callback).Run(MakeResult(*effect, pre_dispatch_status));
    return;
  }
  if (disposition != AccountNetworkResponseDisposition::kCompleted) {
    if (disposition == AccountNetworkResponseDisposition::kRejected) {
      auto binding = oauth_bindings_by_state_.find(state);
      if (binding != oauth_bindings_by_state_.end()) {
        DeleteTransientBestEffort(binding->second.pkce_verifier_handle);
        oauth_bindings_by_state_.erase(binding);
        ScheduleBindingExpiry(NowMonotonicMillisForRoutes());
      }
    }
    std::move(callback).Run(MakeResult(
        *effect, disposition == AccountNetworkResponseDisposition::kRejected
                     ? service::EffectStatus::kDenied
                     : service::EffectStatus::kOutcomeUnknown));
    return;
  }
  auto result = MakeResult(*effect, service::EffectStatus::kCompleted);
  result->network->email_link->flow_id =
      effect->network_request->request_email_link->flow_id;
  result->network->email_link->accepted = true;
  std::move(callback).Run(std::move(result));
}

}  // namespace taffy

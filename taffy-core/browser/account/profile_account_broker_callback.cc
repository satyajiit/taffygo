// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/account/profile_account_broker.h"

#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/strings/string_util.h"
#include "base/time/time.h"
#include "mojo/public/cpp/bindings/callback_helpers.h"
#include "net/base/url_util.h"
#include "taffy/browser/account_plane_configuration.h"
#include "url/gurl.h"

namespace taffy {
namespace {

namespace account_mojom = browser::account::mojom;
namespace service = core_service::mojom;

constexpr char kProductAuthCallback[] = "com.taffygo.browser://auth";
constexpr size_t kPkceS256ChallengeBytes = 43u;

uint64_t NowMonotonicMillisForCallback() {
  const int64_t value = base::TimeTicks::Now().since_origin().InMilliseconds();
  return value < 0 ? 0u : static_cast<uint64_t>(value);
}

bool IsBase64Url(std::string_view value, size_t maximum_size) {
  return !value.empty() && value.size() <= maximum_size &&
         std::ranges::all_of(value, [](const char character) {
           return (character >= 'A' && character <= 'Z') ||
                  (character >= 'a' && character <= 'z') ||
                  (character >= '0' && character <= '9') || character == '-' ||
                  character == '_';
         });
}

bool IsPkceS256Challenge(std::string_view value) {
  return value.size() == kPkceS256ChallengeBytes &&
         IsBase64Url(value, kPkceS256ChallengeBytes);
}

bool IsLowercaseSha256Hex(std::string_view value) {
  return value.size() == service::kGoogleNonceHashHexBytes &&
         std::ranges::all_of(value, [](const char character) {
           return (character >= '0' && character <= '9') ||
                  (character >= 'a' && character <= 'f');
         });
}

std::optional<std::string> ProviderName(service::AccountAuthMethod method) {
  switch (method) {
  case service::AccountAuthMethod::kGoogle:
    return "google";
  case service::AccountAuthMethod::kGithub:
    return "github";
  case service::AccountAuthMethod::kFacebook:
    return "facebook";
  case service::AccountAuthMethod::kEmailLink:
    return std::nullopt;
  }
}

std::optional<std::string> ScopeName(service::AccountScope scope) {
  switch (scope) {
  case service::AccountScope::kOpenId:
    return "openid";
  case service::AccountScope::kEmail:
    return "email";
  case service::AccountScope::kProfile:
    return "profile";
  }
}

} // namespace

std::optional<std::string> ProfileAccountBroker::BuildAuthorizationUrl(
    const service::OAuthSurfaceRequest &request) const {
  if (ValidateAccountPlaneConfiguration(TAFFY_ACCOUNT_API_ORIGIN,
                                        TAFFY_ACCOUNT_PUBLISHABLE_KEY) !=
      AccountPlaneConfigurationStatus::kReady) {
    return std::nullopt;
  }
  const GURL account_origin(TAFFY_ACCOUNT_API_ORIGIN);
  const std::optional<std::string> provider = ProviderName(request.auth_method);
  if (!account_origin.is_valid() || !account_origin.SchemeIs("https") ||
      (!account_origin.path().empty() && account_origin.path() != "/") ||
      account_origin.has_query() || account_origin.has_ref() || !provider ||
      !IsBase64Url(request.state, service::kMaxIdentifierBytes) ||
      !IsBase64Url(request.pkce_verifier_handle,
                   service::kMaxIdentifierBytes) ||
      !IsPkceS256Challenge(request.pkce_challenge) || request.scopes.empty() ||
      request.scopes.size() > service::kMaxAccountScopes) {
    return std::nullopt;
  }
  std::vector<std::string> scopes;
  scopes.reserve(request.scopes.size());
  for (const service::AccountScope scope : request.scopes) {
    std::optional<std::string> name = ScopeName(scope);
    if (!name || std::ranges::find(scopes, *name) != scopes.end()) {
      return std::nullopt;
    }
    scopes.push_back(std::move(*name));
  }
  GURL callback = net::AppendQueryParameter(GURL(kProductAuthCallback), "state",
                                            request.state);
  GURL authorization = account_origin.Resolve("/auth/v1/authorize");
  authorization =
      net::AppendQueryParameter(authorization, "provider", *provider);
  authorization =
      net::AppendQueryParameter(authorization, "redirect_to", callback.spec());
  authorization = net::AppendQueryParameter(authorization, "code_challenge",
                                            request.pkce_challenge);
  authorization =
      net::AppendQueryParameter(authorization, "code_challenge_method", "s256");
  authorization = net::AppendQueryParameter(authorization, "scopes",
                                            base::JoinString(scopes, " "));
  if (!authorization.is_valid() ||
      authorization.spec().size() > account_mojom::kMaxAuthorizationUrlBytes) {
    return std::nullopt;
  }
  return authorization.spec();
}

void ProfileAccountBroker::DispatchAuthSurface(
    service::EffectEnvelopePtr effect, EffectCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!AccountPlaneAllowsProfile(private_profile_)) {
    if (effect) {
      DeleteTransientHandlesForFailedEffect(*effect);
    }
    std::move(callback).Run(
        effect ? MakeResult(*effect, service::EffectStatus::kDenied) : nullptr);
    return;
  }
  if (!effect || !effect->auth_surface || !effect->operation ||
      !AcceptGeneration(effect->operation->service_generation) ||
      !platform_adapter_.is_bound()) {
    if (effect) {
      DeleteTransientHandlesForFailedEffect(*effect);
    }
    std::move(callback).Run(
        effect ? MakeResult(*effect, service::EffectStatus::kUnavailable)
               : nullptr);
    return;
  }
  if (ValidateAccountPlaneConfiguration(TAFFY_ACCOUNT_API_ORIGIN,
                                        TAFFY_ACCOUNT_PUBLISHABLE_KEY) !=
      AccountPlaneConfigurationStatus::kReady) {
    DeleteTransientHandlesForFailedEffect(*effect);
    std::move(callback).Run(
        MakeResult(*effect, service::EffectStatus::kUnavailable));
    return;
  }

  const uint64_t now = NowMonotonicMillisForCallback();
  if (effect->auth_surface->operation_kind ==
      service::AuthSurfaceOperation::kRequestNativeCredential) {
    const service::NativeCredentialSurfaceRequest *request =
        effect->auth_surface->native_credential.get();
    const GoogleServerClientConfigurationStatus client_status =
        ValidateGoogleServerClientConfiguration(
            TAFFY_ACCOUNT_GOOGLE_SERVER_CLIENT_ID);
    if (client_status != GoogleServerClientConfigurationStatus::kReady ||
        !request ||
        request->auth_method != service::AccountAuthMethod::kGoogle ||
        !IsBase64Url(request->raw_nonce_handle, service::kMaxIdentifierBytes) ||
        !IsLowercaseSha256Hex(request->hashed_nonce) ||
        effect->operation->deadline_monotonic_ms <= now) {
      DeleteTransientHandlesForFailedEffect(*effect);
      const service::EffectStatus status =
          client_status == GoogleServerClientConfigurationStatus::kDisabled
              ? service::EffectStatus::kUnavailable
              : service::EffectStatus::kInvalidResult;
      std::move(callback).Run(MakeResult(*effect, status));
      return;
    }
    auto plan = account_mojom::ResolvedNativeCredentialSurfacePlan::New();
    plan->flow_id = request->flow_id;
    plan->auth_method = request->auth_method;
    plan->server_client_id = TAFFY_ACCOUNT_GOOGLE_SERVER_CLIENT_ID;
    plan->hashed_nonce = request->hashed_nonce;
    platform_adapter_->OpenNativeCredentialSurface(
        std::move(plan),
        mojo::WrapCallbackWithDefaultInvokeIfNotRun(
            base::BindOnce(&ProfileAccountBroker::OnNativeSurfaceOpened,
                           weak_factory_.GetWeakPtr(), std::move(effect),
                           std::move(callback)),
            service::EffectStatus::kUnavailable, nullptr));
    return;
  }
  if (effect->auth_surface->operation_kind !=
      service::AuthSurfaceOperation::kOpenOauth) {
    DeleteTransientHandlesForFailedEffect(*effect);
    std::move(callback).Run(
        MakeResult(*effect, service::EffectStatus::kInvalidResult));
    return;
  }

  const service::OAuthSurfaceRequest *request =
      effect->auth_surface->oauth.get();
  const std::optional<std::string> authorization_url =
      request ? BuildAuthorizationUrl(*request) : std::nullopt;
  PruneExpiredBindings(now);
  if (!request || !authorization_url ||
      oauth_bindings_by_state_.size() >= service::kMaxPendingAccountFlows ||
      effect->operation->deadline_monotonic_ms <= now ||
      oauth_bindings_by_state_.contains(request->state)) {
    DeleteTransientHandlesForFailedEffect(*effect);
    std::move(callback).Run(
        MakeResult(*effect, service::EffectStatus::kDenied));
    return;
  }
  PendingOAuthBinding binding{request->flow_id, request->redirect_binding_id,
                              request->pkce_verifier_handle,
                              effect->operation->service_generation,
                              effect->operation->deadline_monotonic_ms};
  oauth_bindings_by_state_.emplace(request->state, std::move(binding));
  ScheduleBindingExpiry(now);
  auto plan = account_mojom::ResolvedOAuthSurfacePlan::New();
  plan->flow_id = request->flow_id;
  plan->authorization_url = *authorization_url;
  const std::string state = request->state;
  platform_adapter_->OpenOAuthSurface(
      std::move(plan),
      mojo::WrapCallbackWithDefaultInvokeIfNotRun(
          base::BindOnce(&ProfileAccountBroker::OnOAuthSurfaceOpened,
                         weak_factory_.GetWeakPtr(), std::move(effect), state,
                         std::move(callback)),
          service::EffectStatus::kUnavailable, nullptr));
}

void ProfileAccountBroker::OnOAuthSurfaceOpened(
    service::EffectEnvelopePtr effect, std::string state,
    EffectCallback callback, service::EffectStatus status,
    service::OAuthSurfaceResultPtr body) {
  const uint64_t now = NowMonotonicMillisForCallback();
  auto binding = oauth_bindings_by_state_.find(state);
  const bool binding_is_live =
      binding != oauth_bindings_by_state_.end() &&
      binding->second.service_generation == active_generation_ &&
      binding->second.deadline_monotonic_ms > now;
  auto result = MakeResult(*effect, status);
  if (result && status == service::EffectStatus::kCompleted && body &&
      body->opened && binding_is_live) {
    result->auth_surface->oauth = std::move(body);
  } else {
    if (binding != oauth_bindings_by_state_.end()) {
      DeleteTransientBestEffort(binding->second.pkce_verifier_handle);
      oauth_bindings_by_state_.erase(binding);
      ScheduleBindingExpiry(now);
    }
    if (result && status == service::EffectStatus::kCompleted) {
      if (effect->operation->deadline_monotonic_ms <= now) {
        result->status = service::EffectStatus::kDeadlineExceeded;
      } else {
        result->status = body ? service::EffectStatus::kDenied
                              : service::EffectStatus::kInvalidResult;
      }
    }
  }
  std::move(callback).Run(std::move(result));
}

void ProfileAccountBroker::OnNativeSurfaceOpened(
    service::EffectEnvelopePtr effect, EffectCallback callback,
    service::EffectStatus status,
    service::NativeCredentialSurfaceResultPtr body) {
  auto result = MakeResult(*effect, status);
  if (result && status == service::EffectStatus::kCompleted && body &&
      body->opened) {
    result->auth_surface->native_credential = std::move(body);
  } else {
    DeleteTransientHandlesForFailedEffect(*effect);
    if (result && status == service::EffectStatus::kCompleted) {
      result->status = body ? service::EffectStatus::kDenied
                            : service::EffectStatus::kInvalidResult;
    }
  }
  std::move(callback).Run(std::move(result));
}

} // namespace taffy

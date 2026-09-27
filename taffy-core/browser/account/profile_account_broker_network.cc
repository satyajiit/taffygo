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
#include "mojo/public/cpp/bindings/callback_helpers.h"
#include "taffy/browser/account_plane_configuration.h"

namespace taffy {
namespace {

namespace account_mojom = browser::account::mojom;
namespace service = core_service::mojom;

void ClearString(std::string *value) {
  if (!value) {
    return;
  }
  std::fill(value->begin(), value->end(), '\0');
  value->clear();
}

void ClearBytes(std::vector<uint8_t> *value) {
  if (!value) {
    return;
  }
  std::ranges::fill(*value, 0u);
  value->clear();
}

std::optional<std::string> WriteJson(base::DictValue body) {
  std::string encoded;
  if (!base::JSONWriter::Write(body, &encoded)) {
    return std::nullopt;
  }
  return encoded;
}

bool IsBase64Url(std::string_view value) {
  return !value.empty() && std::ranges::all_of(value, [](const char character) {
    return (character >= 'A' && character <= 'Z') ||
           (character >= 'a' && character <= 'z') ||
           (character >= '0' && character <= '9') || character == '-' ||
           character == '_';
  });
}

bool IsBoundedIdToken(std::string_view value) {
  return !value.empty() && value.size() <= service::kMaxAccountIdTokenBytes &&
         std::ranges::count(value, '.') == 2 &&
         std::ranges::all_of(value, [](const char character) {
           return (character >= 'A' && character <= 'Z') ||
                  (character >= 'a' && character <= 'z') ||
                  (character >= '0' && character <= '9') || character == '-' ||
                  character == '_' || character == '.';
         });
}

uint64_t NowMonotonicMillisForAccount() {
  const int64_t value = base::TimeTicks::Now().since_origin().InMilliseconds();
  return value < 0 ? 0u : static_cast<uint64_t>(value);
}

} // namespace

void ProfileAccountBroker::DispatchNetwork(service::EffectEnvelopePtr effect,
                                           EffectCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!effect || !effect->operation || !effect->network_request) {
    if (effect) {
      DeleteTransientHandlesForFailedEffect(*effect);
    }
    std::move(callback).Run(nullptr);
    return;
  }
  const bool is_revoke = effect->network_request->operation_kind ==
                             service::AccountNetworkOperation::kRevokeSession &&
                         effect->network_request->revoke_session;
  if (!AccountPlaneAllowsProfile(private_profile_)) {
    DeleteTransientHandlesForFailedEffect(*effect);
    std::move(callback).Run(
        MakeResult(*effect, service::EffectStatus::kDenied));
    return;
  }
  if (!AcceptGeneration(effect->operation->service_generation)) {
    DeleteTransientHandlesForFailedEffect(*effect);
    std::move(callback).Run(
        MakeResult(*effect, is_revoke ? service::EffectStatus::kOutcomeUnknown
                                      : service::EffectStatus::kUnavailable));
    return;
  }
  if (!platform_adapter_.is_bound() ||
      ValidateAccountPlaneConfiguration(TAFFY_ACCOUNT_API_ORIGIN,
                                        TAFFY_ACCOUNT_PUBLISHABLE_KEY) !=
          AccountPlaneConfigurationStatus::kReady) {
    if (is_revoke) {
      DeleteSessionAndFinish(std::move(effect), std::move(callback),
                             service::EffectStatus::kUnavailable);
      return;
    }
    DeleteTransientHandlesForFailedEffect(*effect);
    std::move(callback).Run(
        MakeResult(*effect, service::EffectStatus::kUnavailable));
    return;
  }
  if (effect->operation->deadline_monotonic_ms <=
      NowMonotonicMillisForAccount()) {
    if (is_revoke) {
      DeleteSessionAndFinish(std::move(effect), std::move(callback),
                             service::EffectStatus::kDeadlineExceeded);
      return;
    }
    DeleteTransientHandlesForFailedEffect(*effect);
    std::move(callback).Run(
        MakeResult(*effect, service::EffectStatus::kDeadlineExceeded));
    return;
  }

  const service::NetworkRequestEffect &request = *effect->network_request;
  switch (request.operation_kind) {
  case service::AccountNetworkOperation::kExchangeAuthorizationCode:
    if (request.exchange_authorization_code) {
      platform_adapter_->ConsumeTransient(
          request.exchange_authorization_code->authorization_code_handle,
          mojo::WrapCallbackWithDefaultInvokeIfNotRun(
              base::BindOnce(&ProfileAccountBroker::OnAuthorizationCodeConsumed,
                             weak_factory_.GetWeakPtr(), std::move(effect),
                             std::move(callback)),
              service::EffectStatus::kUnavailable, nullptr));
      return;
    }
    break;
  case service::AccountNetworkOperation::kExchangeNativeCredential:
    if (request.exchange_native_credential) {
      platform_adapter_->ConsumeTransient(
          request.exchange_native_credential->credential_handle,
          mojo::WrapCallbackWithDefaultInvokeIfNotRun(
              base::BindOnce(&ProfileAccountBroker::OnNativeCredentialConsumed,
                             weak_factory_.GetWeakPtr(), std::move(effect),
                             std::move(callback)),
              service::EffectStatus::kUnavailable, nullptr));
      return;
    }
    break;
  case service::AccountNetworkOperation::kRequestEmailLink:
    if (request.request_email_link) {
      StartEmailLinkRequest(std::move(effect), std::move(callback));
      return;
    }
    break;
  case service::AccountNetworkOperation::kRefreshSession:
    if (request.refresh_session &&
        request.refresh_session->expected_rotation > 0u) {
      auto read = account_mojom::SessionMaterialReadRequest::New();
      read->session_handle = request.refresh_session->session_handle;
      read->expected_rotation = request.refresh_session->expected_rotation - 1u;
      platform_adapter_->ReadSession(
          std::move(read),
          mojo::WrapCallbackWithDefaultInvokeIfNotRun(
              base::BindOnce(&ProfileAccountBroker::OnRefreshSessionRead,
                             weak_factory_.GetWeakPtr(), std::move(effect),
                             std::move(callback)),
              service::EffectStatus::kUnavailable, nullptr));
      return;
    }
    break;
  case service::AccountNetworkOperation::kRevokeSession:
    if (request.revoke_session) {
      platform_adapter_->ReadCurrentSession(
          request.revoke_session->session_handle,
          mojo::WrapCallbackWithDefaultInvokeIfNotRun(
              base::BindOnce(
                  &ProfileAccountBroker::OnCurrentSessionReadForRevoke,
                  weak_factory_.GetWeakPtr(), std::move(effect),
                  std::move(callback)),
              service::EffectStatus::kUnavailable, nullptr));
      return;
    }
    break;
  case service::AccountNetworkOperation::kFetchEntitlement:
    // Planned and delivered through the session's dedicated entitlement legs
    // (decision 0082), never dispatched here: this broker speaks to the
    // account plane's origin and a mint is spoken to the managed worker's.
    break;
  }
  DeleteTransientHandlesForFailedEffect(*effect);
  std::move(callback).Run(
      MakeResult(*effect, service::EffectStatus::kInvalidResult));
}

void ProfileAccountBroker::OnAuthorizationCodeConsumed(
    service::EffectEnvelopePtr effect, EffectCallback callback,
    service::EffectStatus status,
    account_mojom::SecretMaterialReadResultPtr result) {
  if (status != service::EffectStatus::kCompleted || !result ||
      result->material.empty()) {
    if (result) {
      ClearBytes(&result->material);
    }
    const auto &request = effect->network_request->exchange_authorization_code;
    if (request) {
      DeleteTransientBestEffort(request->pkce_verifier_handle);
    }
    std::move(callback).Run(MakeResult(*effect, status));
    return;
  }
  const auto &request = effect->network_request->exchange_authorization_code;
  std::string code(result->material.begin(), result->material.end());
  ClearBytes(&result->material);
  platform_adapter_->ConsumeTransient(
      request->pkce_verifier_handle,
      mojo::WrapCallbackWithDefaultInvokeIfNotRun(
          base::BindOnce(&ProfileAccountBroker::OnVerifierConsumed,
                         weak_factory_.GetWeakPtr(), std::move(effect),
                         std::move(callback), std::move(code)),
          service::EffectStatus::kUnavailable, nullptr));
}

void ProfileAccountBroker::OnVerifierConsumed(
    service::EffectEnvelopePtr effect, EffectCallback callback,
    std::string authorization_code, service::EffectStatus status,
    account_mojom::SecretMaterialReadResultPtr result) {
  if (status != service::EffectStatus::kCompleted || !result ||
      result->material.empty()) {
    if (result) {
      ClearBytes(&result->material);
    }
    ClearString(&authorization_code);
    std::move(callback).Run(MakeResult(*effect, status));
    return;
  }
  std::string verifier(result->material.begin(), result->material.end());
  ClearBytes(&result->material);
  base::DictValue body;
  body.Set("auth_code", authorization_code);
  body.Set("code_verifier", verifier);
  std::optional<std::string> encoded = WriteJson(std::move(body));
  ClearString(&authorization_code);
  ClearString(&verifier);
  if (!encoded) {
    std::move(callback).Run(
        MakeResult(*effect, service::EffectStatus::kInvalidResult));
    return;
  }
  const service::AccountAuthMethod method =
      effect->network_request->exchange_authorization_code->auth_method;
  StartTokenRequest(std::move(effect), std::move(callback), std::move(*encoded),
                    method, std::nullopt, 0u, std::nullopt);
}

void ProfileAccountBroker::OnNativeCredentialConsumed(
    service::EffectEnvelopePtr effect, EffectCallback callback,
    service::EffectStatus status,
    account_mojom::SecretMaterialReadResultPtr result) {
  if (status != service::EffectStatus::kCompleted || !result ||
      result->material.empty() ||
      !IsBoundedIdToken(std::string_view(
          reinterpret_cast<const char *>(result->material.data()),
          result->material.size()))) {
    if (result) {
      ClearBytes(&result->material);
    }
    const auto &request = effect->network_request->exchange_native_credential;
    if (request) {
      DeleteTransientBestEffort(request->raw_nonce_handle);
    }
    std::move(callback).Run(
        MakeResult(*effect, status == service::EffectStatus::kCompleted
                                ? service::EffectStatus::kInvalidResult
                                : status));
    return;
  }
  std::string credential(result->material.begin(), result->material.end());
  ClearBytes(&result->material);
  const auto &request = effect->network_request->exchange_native_credential;
  platform_adapter_->ConsumeTransient(
      request->raw_nonce_handle,
      mojo::WrapCallbackWithDefaultInvokeIfNotRun(
          base::BindOnce(&ProfileAccountBroker::OnNativeRawNonceConsumed,
                         weak_factory_.GetWeakPtr(), std::move(effect),
                         std::move(callback), std::move(credential)),
          service::EffectStatus::kUnavailable, nullptr));
}

void ProfileAccountBroker::OnNativeRawNonceConsumed(
    service::EffectEnvelopePtr effect, EffectCallback callback,
    std::string credential, service::EffectStatus status,
    account_mojom::SecretMaterialReadResultPtr result) {
  const bool valid_nonce =
      status == service::EffectStatus::kCompleted && result &&
      result->material.size() == service::kMaxGoogleRawNonceBytes &&
      IsBase64Url(std::string_view(
          reinterpret_cast<const char *>(result->material.data()),
          result->material.size()));
  if (!valid_nonce) {
    if (result) {
      ClearBytes(&result->material);
    }
    ClearString(&credential);
    const auto &request = effect->network_request->exchange_native_credential;
    if (request) {
      DeleteTransientBestEffort(request->raw_nonce_handle);
    }
    std::move(callback).Run(
        MakeResult(*effect, status == service::EffectStatus::kCompleted
                                ? service::EffectStatus::kInvalidResult
                                : status));
    return;
  }
  std::string raw_nonce(result->material.begin(), result->material.end());
  ClearBytes(&result->material);
  base::DictValue body;
  body.Set("provider", "google");
  body.Set("id_token", credential);
  body.Set("nonce", raw_nonce);
  std::optional<std::string> encoded = WriteJson(std::move(body));
  ClearString(&credential);
  ClearString(&raw_nonce);
  if (!encoded) {
    std::move(callback).Run(
        MakeResult(*effect, service::EffectStatus::kInvalidResult));
    return;
  }
  StartTokenRequest(std::move(effect), std::move(callback), std::move(*encoded),
                    service::AccountAuthMethod::kGoogle, std::nullopt, 0u,
                    std::nullopt);
}

} // namespace taffy

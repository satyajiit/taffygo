// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/account/profile_account_broker.h"

#include <algorithm>
#include <limits>
#include <utility>

#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/time/time.h"
#include "mojo/public/cpp/bindings/callback_helpers.h"
#include "taffy/browser/core_account_effect_terminal.h"

namespace taffy {
namespace {

namespace account_mojom = browser::account::mojom;
namespace service = core_service::mojom;

void ClearString(std::string* value) {
  if (!value) {
    return;
  }
  std::fill(value->begin(), value->end(), '\0');
  value->clear();
}

void ClearBytes(std::vector<uint8_t>* value) {
  if (!value) {
    return;
  }
  std::fill(value->begin(), value->end(), 0u);
  value->clear();
}

void ClearValidatedSecrets(
    service::AccountTokenValidationResultPtr* validated) {
  if (!validated || !*validated) {
    return;
  }
  ClearBytes(&(*validated)->access_token);
  ClearBytes(&(*validated)->refresh_token);
}

uint64_t NowMonotonicMillisForSession() {
  const int64_t value = base::TimeTicks::Now().since_origin().InMilliseconds();
  return value < 0 ? 0u : static_cast<uint64_t>(value);
}

bool IsOpaqueHandle(const std::string& value) {
  if (value.empty() || value.size() > service::kMaxIdentifierBytes) {
    return false;
  }
  return std::ranges::all_of(value, [](char character) {
    return (character >= 'A' && character <= 'Z') ||
           (character >= 'a' && character <= 'z') ||
           (character >= '0' && character <= '9') || character == '-' ||
           character == '_';
  });
}

}  // namespace

ProfileAccountBroker::SessionPayload::SessionPayload() = default;

ProfileAccountBroker::SessionPayload::SessionPayload(
    SessionPayload&& other) noexcept
    : access_token(std::move(other.access_token)),
      refresh_token(std::move(other.refresh_token)),
      account_subject(std::move(other.account_subject)),
      auth_method(other.auth_method) {
  ClearString(&other.access_token);
  ClearString(&other.refresh_token);
}

ProfileAccountBroker::SessionPayload&
ProfileAccountBroker::SessionPayload::operator=(SessionPayload&& other) noexcept {
  if (this == &other) {
    return *this;
  }
  ClearString(&access_token);
  ClearString(&refresh_token);
  access_token = std::move(other.access_token);
  refresh_token = std::move(other.refresh_token);
  account_subject = std::move(other.account_subject);
  auth_method = other.auth_method;
  ClearString(&other.access_token);
  ClearString(&other.refresh_token);
  return *this;
}

ProfileAccountBroker::SessionPayload::~SessionPayload() {
  ClearString(&access_token);
  ClearString(&refresh_token);
}

std::optional<std::vector<uint8_t>> ProfileAccountBroker::EncodeSessionPayload(
    const SessionPayload& payload) const {
  if (payload.access_token.empty() || payload.refresh_token.empty() ||
      payload.account_subject.empty() ||
      payload.account_subject.size() > service::kMaxIdentifierBytes) {
    return std::nullopt;
  }
  base::DictValue value;
  value.Set("access_token", payload.access_token);
  value.Set("refresh_token", payload.refresh_token);
  value.Set("account_subject", payload.account_subject);
  value.Set("auth_method", static_cast<int>(payload.auth_method));
  std::string encoded;
  if (!base::JSONWriter::Write(value, &encoded) ||
      encoded.size() > account_mojom::kMaxAccountSessionMaterialBytes) {
    ClearString(&encoded);
    return std::nullopt;
  }
  std::vector<uint8_t> bytes(encoded.begin(), encoded.end());
  ClearString(&encoded);
  return bytes;
}

std::optional<ProfileAccountBroker::SessionPayload>
ProfileAccountBroker::DecodeSessionPayload(
    const std::vector<uint8_t>& bytes) const {
  if (bytes.empty() ||
      bytes.size() > account_mojom::kMaxAccountSessionMaterialBytes) {
    return std::nullopt;
  }
  std::string encoded(bytes.begin(), bytes.end());
  std::optional<base::DictValue> value =
      base::JSONReader::ReadDict(encoded, base::JSON_PARSE_RFC, 8);
  ClearString(&encoded);
  if (!value || value->size() != 4u) {
    return std::nullopt;
  }
  const std::string* access = value->FindString("access_token");
  const std::string* refresh = value->FindString("refresh_token");
  const std::string* subject = value->FindString("account_subject");
  const std::optional<int> method = value->FindInt("auth_method");
  if (!access || access->empty() || !refresh || refresh->empty() || !subject ||
      subject->empty() || subject->size() > service::kMaxIdentifierBytes ||
      !method || *method < static_cast<int>(service::AccountAuthMethod::kGoogle) ||
      *method > static_cast<int>(service::AccountAuthMethod::kFacebook)) {
    return std::nullopt;
  }
  SessionPayload result;
  result.access_token = *access;
  result.refresh_token = *refresh;
  result.account_subject = *subject;
  result.auth_method = static_cast<service::AccountAuthMethod>(*method);
  return result;
}

void ProfileAccountBroker::OnTokenValidated(
    service::EffectEnvelopePtr effect,
    EffectCallback callback,
    service::AccountAuthMethod method,
    std::optional<std::string> previous_handle,
    uint64_t target_rotation,
    std::optional<std::string> expected_subject,
    service::AccountTokenValidationResultPtr validated) {
  const uint64_t now = NowMonotonicMillisForSession();
  if (!effect || !effect->operation || !effect->network_request ||
      effect->operation->service_generation != active_generation_ ||
      effect->operation->deadline_monotonic_ms <= now ||
      !platform_adapter_.is_bound()) {
    ClearValidatedSecrets(&validated);
    if (effect && previous_handle) {
      DeleteSessionAndFinish(std::move(effect), std::move(callback),
                             service::EffectStatus::kOutcomeUnknown);
    } else if (effect) {
      std::move(callback).Run(
          MakeResult(*effect, service::EffectStatus::kOutcomeUnknown));
    } else {
      std::move(callback).Run(nullptr);
    }
    return;
  }
  const bool matches =
      validated &&
      validated->status == service::AccountTokenValidationStatus::kValidated &&
      validated->operation_id == effect->operation->operation_id &&
      validated->operation_kind == effect->network_request->operation_kind &&
      validated->auth_method == method &&
      validated->target_rotation == target_rotation &&
      !validated->account_subject.empty() &&
      validated->account_subject.size() <= service::kMaxIdentifierBytes &&
      validated->expires_in_seconds > 0u &&
      validated->expires_in_seconds <=
          service::kMaxAccountSessionLifetimeSeconds &&
      !validated->access_token.empty() &&
      validated->access_token.size() <= service::kMaxAccountAccessTokenBytes &&
      !validated->refresh_token.empty() &&
      validated->refresh_token.size() <=
          service::kMaxAccountRefreshTokenBytes &&
      (!expected_subject ||
       *expected_subject == validated->account_subject);
  if (!matches) {
    ClearValidatedSecrets(&validated);
    if (previous_handle) {
      DeleteSessionAndFinish(std::move(effect), std::move(callback),
                             service::EffectStatus::kOutcomeUnknown);
    } else {
      std::move(callback).Run(
          MakeResult(*effect, service::EffectStatus::kOutcomeUnknown));
    }
    return;
  }

  SessionPayload payload;
  payload.access_token.assign(validated->access_token.begin(),
                              validated->access_token.end());
  payload.refresh_token.assign(validated->refresh_token.begin(),
                               validated->refresh_token.end());
  payload.account_subject = validated->account_subject;
  payload.auth_method = method;
  ClearBytes(&validated->access_token);
  ClearBytes(&validated->refresh_token);
  std::optional<std::vector<uint8_t>> material = EncodeSessionPayload(payload);
  if (!material) {
    if (previous_handle) {
      DeleteSessionAndFinish(std::move(effect), std::move(callback),
                             service::EffectStatus::kOutcomeUnknown);
    } else {
      std::move(callback).Run(
          MakeResult(*effect, service::EffectStatus::kOutcomeUnknown));
    }
    return;
  }
  AccountIdentity identity{validated->email, validated->display_name};
  const uint64_t expires_ms = validated->expires_in_seconds * 1000u;
  const uint64_t expires_at =
      now > std::numeric_limits<uint64_t>::max() - expires_ms
          ? std::numeric_limits<uint64_t>::max()
          : now + expires_ms;
  auto write = account_mojom::SessionMaterialWriteRequest::New();
  write->previous_handle = std::move(previous_handle);
  write->expected_rotation = target_rotation;
  write->material = std::move(*material);
  platform_adapter_->RotateSession(
      std::move(write),
      mojo::WrapCallbackWithDefaultInvokeIfNotRun(
          base::BindOnce(&ProfileAccountBroker::OnSessionRotated,
                         weak_factory_.GetWeakPtr(), std::move(effect),
                         std::move(callback), validated->account_subject,
                         method, expires_at, std::move(identity)),
          service::EffectStatus::kUnavailable, nullptr));
}

void ProfileAccountBroker::DeleteExactSessionAndFinish(
    service::EffectEnvelopePtr effect,
    EffectCallback callback,
    std::string session_handle,
    uint64_t expected_rotation,
    service::EffectStatus terminal_status) {
  if (!effect) {
    std::move(callback).Run(nullptr);
    return;
  }
  if (!platform_adapter_.is_bound() || !IsOpaqueHandle(session_handle)) {
    std::move(callback).Run(MakeResult(*effect, terminal_status));
    return;
  }
  auto request = account_mojom::SessionMaterialReadRequest::New();
  request->session_handle = std::move(session_handle);
  request->expected_rotation = expected_rotation;
  platform_adapter_->DeleteSessionVersion(
      std::move(request),
      mojo::WrapCallbackWithDefaultInvokeIfNotRun(
          base::BindOnce(&ProfileAccountBroker::OnExactSessionDeleted,
                         weak_factory_.GetWeakPtr(), std::move(effect),
                         std::move(callback), terminal_status),
          service::EffectStatus::kUnavailable, false));
}

void ProfileAccountBroker::OnExactSessionDeleted(
    service::EffectEnvelopePtr effect,
    EffectCallback callback,
    service::EffectStatus terminal_status,
    service::EffectStatus,
    bool) {
  std::move(callback).Run(MakeResult(*effect, terminal_status));
}

void ProfileAccountBroker::OnSessionRotated(
    service::EffectEnvelopePtr effect,
    EffectCallback callback,
    std::string account_subject,
    service::AccountAuthMethod auth_method,
    uint64_t expires_at_monotonic_ms,
    AccountIdentity identity,
    service::EffectStatus status,
    account_mojom::SessionMaterialWriteResultPtr stored) {
  if (!effect || !effect->operation || !effect->network_request) {
    std::move(callback).Run(nullptr);
    return;
  }
  const auto operation_kind = effect->network_request->operation_kind;
  const bool is_refresh =
      operation_kind == service::AccountNetworkOperation::kRefreshSession;
  const auto& refresh = effect->network_request->refresh_session;
  const uint64_t expected_rotation = is_refresh && refresh
                                         ? refresh->expected_rotation
                                         : 0u;
  std::optional<std::string> cleanup_handle;
  if (is_refresh && refresh && IsOpaqueHandle(refresh->session_handle)) {
    cleanup_handle = refresh->session_handle;
  } else if (!is_refresh && status == service::EffectStatus::kCompleted &&
             stored && IsOpaqueHandle(stored->session_handle) &&
             stored->rotation == 0u) {
    cleanup_handle = stored->session_handle;
  }
  const auto finish_outcome_unknown =
      [&](service::EffectEnvelopePtr failed_effect,
          EffectCallback failed_callback) {
        if (cleanup_handle) {
          DeleteExactSessionAndFinish(
              std::move(failed_effect), std::move(failed_callback),
              std::move(*cleanup_handle), expected_rotation,
              service::EffectStatus::kOutcomeUnknown);
          return;
        }
        std::move(failed_callback)
            .Run(MakeResult(*failed_effect,
                            service::EffectStatus::kOutcomeUnknown));
      };
  if (effect->operation->service_generation != active_generation_ ||
      effect->operation->deadline_monotonic_ms <=
          NowMonotonicMillisForSession()) {
    finish_outcome_unknown(std::move(effect), std::move(callback));
    return;
  }
  const bool matches_previous =
      !is_refresh ||
      (refresh && stored && stored->session_handle == refresh->session_handle);
  if (status != service::EffectStatus::kCompleted || !stored ||
      !IsOpaqueHandle(stored->session_handle) ||
      stored->rotation != expected_rotation || !matches_previous) {
    finish_outcome_unknown(std::move(effect), std::move(callback));
    return;
  }
  auto result = MakeResult(*effect, service::EffectStatus::kCompleted);
  auto receipt = service::AccountSessionReceipt::New(
      stored->session_handle, std::move(account_subject),
      expires_at_monotonic_ms, stored->rotation, auth_method,
      std::move(identity.email), std::move(identity.display_name));
  switch (operation_kind) {
    case service::AccountNetworkOperation::kExchangeAuthorizationCode:
      result->network->authorization_code_session = std::move(receipt);
      break;
    case service::AccountNetworkOperation::kExchangeNativeCredential:
      result->network->native_credential_session = std::move(receipt);
      break;
    case service::AccountNetworkOperation::kRefreshSession:
      result->network->refreshed_session = std::move(receipt);
      break;
    default:
      finish_outcome_unknown(std::move(effect), std::move(callback));
      return;
  }
  std::move(callback).Run(std::move(result));
}

void ProfileAccountBroker::DeleteSessionAndFinish(
    service::EffectEnvelopePtr effect,
    EffectCallback callback,
    service::EffectStatus terminal_status) {
  if (!platform_adapter_.is_bound()) {
    OnCanonicalSessionCleared(
        std::move(effect), std::move(callback), terminal_status,
        service::EffectStatus::kUnavailable, false);
    return;
  }
  platform_adapter_->ClearSession(
      mojo::WrapCallbackWithDefaultInvokeIfNotRun(
          base::BindOnce(&ProfileAccountBroker::OnCanonicalSessionCleared,
                         weak_factory_.GetWeakPtr(), std::move(effect),
                         std::move(callback), terminal_status),
          service::EffectStatus::kUnavailable, false));
}

void ProfileAccountBroker::OnCanonicalSessionCleared(
    service::EffectEnvelopePtr effect,
    EffectCallback callback,
    service::EffectStatus terminal_status,
    service::EffectStatus storage_status,
    bool cleared) {
  const CanonicalSessionClearDecision decision = ResolveCanonicalSessionClear(
      terminal_status, storage_status, cleared);
  auto result = MakeResult(*effect, decision.terminal_status);
  if (decision.mark_completed_revoke_deleted && result && result->network &&
      result->network->operation_kind ==
          service::AccountNetworkOperation::kRevokeSession &&
      result->network->revoked_session) {
    result->network->revoked_session->deleted = true;
  }
  std::move(callback).Run(std::move(result));
}

}  // namespace taffy

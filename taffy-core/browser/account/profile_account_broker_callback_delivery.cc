// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/account/profile_account_broker.h"

#include <algorithm>
#include <limits>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/location.h"
#include "base/time/time.h"
#include "crypto/secure_util.h"
#include "mojo/public/cpp/bindings/callback_helpers.h"
#include "taffy/browser/account_plane_configuration.h"
#include "taffy/browser/providerauth/auth_callback_parser.h"

namespace taffy {
namespace {

namespace service = core_service::mojom;

uint64_t NowMonotonicMillisForCallback() {
  const int64_t value = base::TimeTicks::Now().since_origin().InMilliseconds();
  return value < 0 ? 0u : static_cast<uint64_t>(value);
}

void SecureClear(std::string *value) {
  if (!value) {
    return;
  }
  crypto::SecureZeroBuffer(base::as_writable_byte_span(*value));
  value->clear();
}

void SecureClearAuthorizationCode(ParsedAccountAuthCallback *callback) {
  if (callback && callback->authorization_code) {
    SecureClear(&*callback->authorization_code);
  }
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

service::AuthCallbackStatus
ProjectCallbackOutcome(AccountAuthCallbackOutcome outcome) {
  switch (outcome) {
  case AccountAuthCallbackOutcome::kAuthorizationCode:
    return service::AuthCallbackStatus::kAuthorizationCode;
  case AccountAuthCallbackOutcome::kDenied:
    return service::AuthCallbackStatus::kDenied;
  case AccountAuthCallbackOutcome::kProviderError:
    return service::AuthCallbackStatus::kProviderError;
  }
}

ProfileAccountBroker::RedirectSubmissionCallback NoSubmissionCleanup() {
  return base::BindOnce([](bool) {});
}

} // namespace

bool ProfileAccountBroker::DeliverAuthCallback(std::string raw_uri,
                                               RedirectCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!AccountPlaneAllowsProfile(private_profile_)) {
    return false;
  }
  std::optional<ParsedAccountAuthCallback> parsed =
      ParseAccountAuthCallback(raw_uri);
  SecureClear(&raw_uri);
  const uint64_t now = NowMonotonicMillisForCallback();
  PruneExpiredBindings(now);
  if (!parsed) {
    return false;
  }
  auto found = oauth_bindings_by_state_.find(parsed->state);
  if (found == oauth_bindings_by_state_.end() ||
      found->second.deadline_monotonic_ms <= now) {
    SecureClearAuthorizationCode(&*parsed);
    return false;
  }
  if (found->second.service_generation != active_generation_) {
    SecureClearAuthorizationCode(&*parsed);
    DeleteTransientBestEffort(found->second.pkce_verifier_handle);
    oauth_bindings_by_state_.erase(found);
    ScheduleBindingExpiry(now);
    return false;
  }
  PendingOAuthBinding binding = std::move(found->second);
  oauth_bindings_by_state_.erase(found);
  ScheduleBindingExpiry(now);
  if (parsed->outcome != AccountAuthCallbackOutcome::kAuthorizationCode) {
    SecureClearAuthorizationCode(&*parsed);
    DeleteTransientBestEffort(binding.pkce_verifier_handle);
    auto receipt = service::AuthCallbackCommand::New();
    receipt->flow_id = binding.flow_id;
    receipt->redirect_binding_id = binding.redirect_binding_id;
    receipt->returned_state = parsed->state;
    receipt->status = ProjectCallbackOutcome(parsed->outcome);
    std::move(callback).Run(binding.service_generation, std::move(receipt),
                            NoSubmissionCleanup());
    return true;
  }
  if (!platform_adapter_.is_bound() || !parsed->authorization_code) {
    SecureClearAuthorizationCode(&*parsed);
    DeleteTransientBestEffort(binding.pkce_verifier_handle);
    std::move(callback).Run(binding.service_generation, nullptr,
                            NoSubmissionCleanup());
    return true;
  }
  std::vector<uint8_t> code(parsed->authorization_code->begin(),
                            parsed->authorization_code->end());
  SecureClearAuthorizationCode(&*parsed);
  platform_adapter_->WriteAuthorizationCode(
      binding.flow_id, std::move(code),
      mojo::WrapCallbackWithDefaultInvokeIfNotRun(
          base::BindOnce(&ProfileAccountBroker::OnAuthorizationCodeStored,
                         weak_factory_.GetWeakPtr(), std::move(binding),
                         parsed->state, std::move(callback)),
          service::EffectStatus::kUnavailable, std::nullopt));
  return true;
}

void ProfileAccountBroker::OnAuthorizationCodeStored(
    PendingOAuthBinding binding, std::string returned_state,
    RedirectCallback callback, service::EffectStatus status,
    const std::optional<std::string> &secret_handle) {
  if (status != service::EffectStatus::kCompleted || !secret_handle ||
      !IsBase64Url(*secret_handle, service::kMaxIdentifierBytes)) {
    if (secret_handle && !secret_handle->empty()) {
      DeleteTransientBestEffort(*secret_handle);
    }
    DeleteTransientBestEffort(binding.pkce_verifier_handle);
    std::move(callback).Run(binding.service_generation, nullptr,
                            NoSubmissionCleanup());
    return;
  }
  if (binding.service_generation != active_generation_ ||
      binding.deadline_monotonic_ms <= NowMonotonicMillisForCallback()) {
    DeleteTransientBestEffort(*secret_handle);
    DeleteTransientBestEffort(binding.pkce_verifier_handle);
    std::move(callback).Run(binding.service_generation, nullptr,
                            NoSubmissionCleanup());
    return;
  }
  auto receipt = service::AuthCallbackCommand::New();
  receipt->flow_id = std::move(binding.flow_id);
  receipt->redirect_binding_id = std::move(binding.redirect_binding_id);
  receipt->returned_state = std::move(returned_state);
  receipt->status = service::AuthCallbackStatus::kAuthorizationCode;
  receipt->authorization_code_handle = *secret_handle;
  RedirectSubmissionCallback submitted =
      base::BindOnce(&ProfileAccountBroker::OnAuthCallbackSubmitted,
                     weak_factory_.GetWeakPtr(), *secret_handle,
                     std::move(binding.pkce_verifier_handle));
  std::move(callback).Run(binding.service_generation, std::move(receipt),
                          std::move(submitted));
}

void ProfileAccountBroker::OnAuthCallbackSubmitted(
    std::string authorization_code_handle, std::string pkce_verifier_handle,
    bool submitted) {
  if (submitted) {
    return;
  }
  DeleteTransientBestEffort(std::move(authorization_code_handle));
  DeleteTransientBestEffort(std::move(pkce_verifier_handle));
}

void ProfileAccountBroker::PruneExpiredBindings(uint64_t now_monotonic_ms) {
  for (auto it = oauth_bindings_by_state_.begin();
       it != oauth_bindings_by_state_.end();) {
    if (it->second.deadline_monotonic_ms <= now_monotonic_ms) {
      const std::string state = it->first;
      PendingOAuthBinding binding = std::move(it->second);
      it = oauth_bindings_by_state_.erase(it);
      DeleteTransientBestEffort(binding.pkce_verifier_handle);
      if (flow_terminal_callback_) {
        auto receipt = service::AuthCallbackCommand::New();
        receipt->flow_id = std::move(binding.flow_id);
        receipt->redirect_binding_id = std::move(binding.redirect_binding_id);
        receipt->returned_state = state;
        receipt->status = service::AuthCallbackStatus::kDeadlineExceeded;
        flow_terminal_callback_.Run(binding.service_generation,
                                    std::move(receipt));
      }
    } else {
      ++it;
    }
  }
  ScheduleBindingExpiry(now_monotonic_ms);
}

void ProfileAccountBroker::ScheduleBindingExpiry(uint64_t now_monotonic_ms) {
  oauth_binding_timer_.Stop();
  if (oauth_bindings_by_state_.empty()) {
    return;
  }
  uint64_t deadline = std::numeric_limits<uint64_t>::max();
  for (const auto &[_, binding] : oauth_bindings_by_state_) {
    deadline = std::min(deadline, binding.deadline_monotonic_ms);
  }
  const base::TimeDelta delay =
      deadline <= now_monotonic_ms
          ? base::TimeDelta()
          : base::Milliseconds(deadline - now_monotonic_ms);
  oauth_binding_timer_.Start(FROM_HERE, delay, this,
                             &ProfileAccountBroker::OnBindingExpiry);
}

void ProfileAccountBroker::OnBindingExpiry() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  PruneExpiredBindings(NowMonotonicMillisForCallback());
}

} // namespace taffy

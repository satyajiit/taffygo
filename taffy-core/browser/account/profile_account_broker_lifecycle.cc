// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/account/profile_account_broker.h"

#include <utility>

#include "base/functional/bind.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"

namespace taffy {
namespace {

namespace service = core_service::mojom;

} // namespace

bool ProfileAccountBroker::has_pending_auth_flow() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return !oauth_bindings_by_state_.empty();
}

bool ProfileAccountBroker::AcceptGeneration(uint64_t generation) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (generation == 0u) {
    return false;
  }
  if (active_generation_ == 0u) {
    active_generation_ = generation;
  }
  return active_generation_ == generation;
}

void ProfileAccountBroker::InstallPlatformDisconnectHandler() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!platform_adapter_.is_bound()) {
    return;
  }
  platform_adapter_.set_disconnect_handler(base::BindOnce(
      [](base::WeakPtr<ProfileAccountBroker> broker) {
        if (!broker) {
          return;
        }
        broker->SettleOAuthBindings(
            service::AuthCallbackStatus::kPlatformUnavailable);
        broker->CancelNetworkRequests();
        broker->active_generation_ = 0u;
      },
      weak_factory_.GetWeakPtr()));
}

void ProfileAccountBroker::CancelNetworkRequests() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  decltype(network_loaders_) loaders;
  loaders.swap(network_loaders_);
  loaders.clear();
}

void ProfileAccountBroker::SettleOAuthBindings(
    service::AuthCallbackStatus terminal_status) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  oauth_binding_timer_.Stop();
  auto bindings = std::move(oauth_bindings_by_state_);
  oauth_bindings_by_state_.clear();
  for (auto &[state, binding] : bindings) {
    DeleteTransientBestEffort(binding.pkce_verifier_handle);
    if (!flow_terminal_callback_) {
      continue;
    }
    auto receipt = service::AuthCallbackCommand::New();
    receipt->flow_id = std::move(binding.flow_id);
    receipt->redirect_binding_id = std::move(binding.redirect_binding_id);
    receipt->returned_state = state;
    receipt->status = terminal_status;
    flow_terminal_callback_.Run(binding.service_generation, std::move(receipt));
  }
}

void ProfileAccountBroker::DeleteTransientBestEffort(
    std::string secret_handle) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!platform_adapter_.is_bound() || secret_handle.empty()) {
    return;
  }
  auto request = service::DeleteSecretHandleRequest::New();
  request->secret_handle = std::move(secret_handle);
  platform_adapter_->DeleteSecret(
      std::move(request),
      base::BindOnce(
          [](service::EffectStatus, service::DeletedSecretHandleResultPtr) {}));
}

void ProfileAccountBroker::DeleteTransientHandlesForFailedEffect(
    const service::EffectEnvelope &effect) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (effect.auth_surface && effect.auth_surface->oauth) {
    DeleteTransientBestEffort(effect.auth_surface->oauth->pkce_verifier_handle);
  }
  if (effect.auth_surface && effect.auth_surface->native_credential) {
    DeleteTransientBestEffort(
        effect.auth_surface->native_credential->raw_nonce_handle);
  }
  if (!effect.network_request) {
    return;
  }
  const service::NetworkRequestEffect &request = *effect.network_request;
  if (request.exchange_authorization_code) {
    DeleteTransientBestEffort(
        request.exchange_authorization_code->authorization_code_handle);
    DeleteTransientBestEffort(
        request.exchange_authorization_code->pkce_verifier_handle);
  } else if (request.exchange_native_credential) {
    DeleteTransientBestEffort(
        request.exchange_native_credential->credential_handle);
    DeleteTransientBestEffort(
        request.exchange_native_credential->raw_nonce_handle);
  } else if (request.request_email_link) {
    DeleteTransientBestEffort(request.request_email_link->pkce_verifier_handle);
  }
}

void ProfileAccountBroker::CancelGeneration(uint64_t generation) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (generation == 0u || active_generation_ != generation) {
    return;
  }
  for (const auto &[_, binding] : oauth_bindings_by_state_) {
    DeleteTransientBestEffort(binding.pkce_verifier_handle);
  }
  oauth_bindings_by_state_.clear();
  oauth_binding_timer_.Stop();
  CancelNetworkRequests();
  if (platform_adapter_.is_bound()) {
    platform_adapter_->ClearTransient(
        base::BindOnce([](service::EffectStatus, uint32_t) {}));
  }
  active_generation_ = 0u;
  weak_factory_.InvalidateWeakPtrs();
  InstallPlatformDisconnectHandler();
}

} // namespace taffy

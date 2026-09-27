// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <algorithm>
#include <optional>
#include <ranges>
#include <string>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "mojo/public/cpp/bindings/callback_helpers.h"
#include "taffy/browser/account/profile_account_broker.h"

// The provider-subscription passthroughs (decision 0081). Each is a narrow
// question to the one Android platform adapter this class owns; the provider
// auth broker holds the flow, this file holds nothing. Material passing
// through is zeroed on every path, success and failure alike.

namespace taffy {
namespace {

namespace account_mojom = browser::account::mojom;
namespace service = core_service::mojom;

void ZeroRecord(account_mojom::ProviderOauthRecord *record) {
  if (!record) {
    return;
  }
  std::ranges::fill(record->access_token, 0u);
  record->access_token.clear();
  if (record->refresh_token) {
    std::ranges::fill(*record->refresh_token, 0u);
    record->refresh_token->clear();
  }
}

}  // namespace

void ProfileAccountBroker::OpenProviderAuthSurface(
    const std::string &flow_id, const std::string &authorization_url,
    base::OnceCallback<void(bool)> callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!callback) {
    return;
  }
  if (private_profile_ || flow_id.empty() || authorization_url.empty() ||
      !platform_adapter_.is_bound()) {
    std::move(callback).Run(false);
    return;
  }
  auto plan = account_mojom::ResolvedOAuthSurfacePlan::New();
  plan->flow_id = flow_id;
  plan->authorization_url = authorization_url;
  platform_adapter_->OpenOAuthSurface(
      std::move(plan),
      mojo::WrapCallbackWithDefaultInvokeIfNotRun(
          base::BindOnce(
              [](base::OnceCallback<void(bool)> done,
                 service::EffectStatus status,
                 service::OAuthSurfaceResultPtr result) {
                std::move(done).Run(status == service::EffectStatus::kCompleted &&
                                    result && result->opened);
              },
              std::move(callback)),
          service::EffectStatus::kUnavailable, nullptr));
}

void ProfileAccountBroker::SealProviderAuthorizationCode(
    const std::string &flow_id, std::string code,
    base::OnceCallback<void(std::optional<std::string>)> callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!callback) {
    std::fill(code.begin(), code.end(), '\0');
    return;
  }
  if (private_profile_ || flow_id.empty() || code.empty() ||
      !platform_adapter_.is_bound()) {
    std::fill(code.begin(), code.end(), '\0');
    std::move(callback).Run(std::nullopt);
    return;
  }
  std::vector<uint8_t> material(code.begin(), code.end());
  std::fill(code.begin(), code.end(), '\0');
  platform_adapter_->WriteAuthorizationCode(
      flow_id, std::move(material),
      mojo::WrapCallbackWithDefaultInvokeIfNotRun(
          base::BindOnce(
              [](base::OnceCallback<void(std::optional<std::string>)> done,
                 service::EffectStatus status,
                 const std::optional<std::string> &handle) {
                if (status != service::EffectStatus::kCompleted || !handle ||
                    handle->empty()) {
                  std::move(done).Run(std::nullopt);
                  return;
                }
                std::move(done).Run(*handle);
              },
              std::move(callback)),
          service::EffectStatus::kUnavailable, std::nullopt));
}

void ProfileAccountBroker::ConsumeTransientCredential(
    const std::string &credential_handle,
    base::OnceCallback<void(std::optional<std::string>)> callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!callback) {
    return;
  }
  if (private_profile_ || credential_handle.empty() ||
      !platform_adapter_.is_bound()) {
    std::move(callback).Run(std::nullopt);
    return;
  }
  platform_adapter_->ConsumeTransient(
      credential_handle,
      mojo::WrapCallbackWithDefaultInvokeIfNotRun(
          base::BindOnce(
              [](base::OnceCallback<void(std::optional<std::string>)> done,
                 service::EffectStatus status,
                 account_mojom::SecretMaterialReadResultPtr result) {
                if (status != service::EffectStatus::kCompleted || !result ||
                    result->material.empty()) {
                  if (result) {
                    std::ranges::fill(result->material, 0u);
                    result->material.clear();
                  }
                  std::move(done).Run(std::nullopt);
                  return;
                }
                std::string credential(result->material.begin(),
                                       result->material.end());
                std::ranges::fill(result->material, 0u);
                result->material.clear();
                std::move(done).Run(std::move(credential));
              },
              std::move(callback)),
          service::EffectStatus::kUnavailable, nullptr));
}

void ProfileAccountBroker::DeleteProviderTransient(const std::string &handle) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (handle.empty() || !platform_adapter_.is_bound()) {
    return;
  }
  auto request = service::DeleteSecretHandleRequest::New();
  request->secret_handle = handle;
  platform_adapter_->DeleteSecret(
      std::move(request),
      base::BindOnce([](service::EffectStatus,
                        service::DeletedSecretHandleResultPtr) {}));
}

void ProfileAccountBroker::StoreProviderOauthRecord(
    const std::string &provider_id,
    account_mojom::ProviderOauthRecordPtr record, bool rotation,
    base::OnceCallback<void(bool)> callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!callback) {
    ZeroRecord(record.get());
    return;
  }
  if (private_profile_ || provider_id.empty() || !record ||
      record->access_token.empty() || !platform_adapter_.is_bound()) {
    ZeroRecord(record.get());
    std::move(callback).Run(false);
    return;
  }
  platform_adapter_->StoreProviderOauthRecord(
      provider_id, std::move(record), rotation,
      mojo::WrapCallbackWithDefaultInvokeIfNotRun(
          base::BindOnce(
              [](base::OnceCallback<void(bool)> done,
                 service::EffectStatus status, bool stored) {
                std::move(done).Run(
                    status == service::EffectStatus::kCompleted && stored);
              },
              std::move(callback)),
          service::EffectStatus::kUnavailable, false));
}

void ProfileAccountBroker::StoreProviderApiKey(
    const std::string &provider_id, std::string key,
    base::OnceCallback<void(bool)> callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!callback) {
    std::fill(key.begin(), key.end(), '\0');
    return;
  }
  if (private_profile_ || provider_id.empty() || key.empty() ||
      !platform_adapter_.is_bound()) {
    std::fill(key.begin(), key.end(), '\0');
    std::move(callback).Run(false);
    return;
  }
  std::vector<uint8_t> material(key.begin(), key.end());
  std::fill(key.begin(), key.end(), '\0');
  platform_adapter_->StoreProviderApiKey(
      provider_id, std::move(material),
      mojo::WrapCallbackWithDefaultInvokeIfNotRun(
          base::BindOnce(
              [](base::OnceCallback<void(bool)> done,
                 service::EffectStatus status, bool stored) {
                std::move(done).Run(
                    status == service::EffectStatus::kCompleted && stored);
              },
              std::move(callback)),
          service::EffectStatus::kUnavailable, false));
}

void ProfileAccountBroker::NotifyProviderFlowEvent(
    account_mojom::ProviderFlowEventPtr event) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (private_profile_ || !event || !platform_adapter_.is_bound()) {
    return;
  }
  platform_adapter_->OnProviderFlowEvent(
      std::move(event), base::BindOnce([](service::EffectStatus) {}));
}

void ProfileAccountBroker::ResolveProviderAccess(
    const std::string &provider_id, const std::string &credential_handle,
    base::OnceCallback<void(account_mojom::ProviderAccessResultPtr)> callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!callback) {
    return;
  }
  if (private_profile_ || provider_id.empty() || credential_handle.empty() ||
      !platform_adapter_.is_bound()) {
    std::move(callback).Run(nullptr);
    return;
  }
  platform_adapter_->ResolveProviderAccess(
      provider_id, credential_handle,
      mojo::WrapCallbackWithDefaultInvokeIfNotRun(
          base::BindOnce(
              [](base::OnceCallback<void(account_mojom::ProviderAccessResultPtr)>
                     done,
                 service::EffectStatus status,
                 account_mojom::ProviderAccessResultPtr result) {
                if (status != service::EffectStatus::kCompleted) {
                  std::move(done).Run(nullptr);
                  return;
                }
                std::move(done).Run(std::move(result));
              },
              std::move(callback)),
          service::EffectStatus::kUnavailable, nullptr));
}

}  // namespace taffy

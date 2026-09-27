// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/account/fake_profile_platform_adapter.h"

#include <utility>

#include "base/strings/string_number_conversions.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"

namespace taffy {

namespace account_mojom = browser::account::mojom;
namespace service = core_service::mojom;

FakeProfilePlatformAdapter::FakeProfilePlatformAdapter() = default;

FakeProfilePlatformAdapter::~FakeProfilePlatformAdapter() = default;

mojo::PendingRemote<account_mojom::TaffyProfilePlatformAdapter>
FakeProfilePlatformAdapter::BindNewPipeAndPassRemote() {
  return receiver_.BindNewPipeAndPassRemote();
}

void FakeProfilePlatformAdapter::EnqueueAccessResult(
    account_mojom::ProviderAccessResultPtr result) {
  access_results_.push_back(std::move(result));
}

void FakeProfilePlatformAdapter::GenerateEntropy(
    service::GenerateEntropyRequestPtr request,
    GenerateEntropyCallback callback) {
  std::move(callback).Run(service::EffectStatus::kUnavailable, nullptr);
}

void FakeProfilePlatformAdapter::WriteTransient(
    service::WriteTransientSecretRequestPtr request,
    WriteTransientCallback callback) {
  std::move(callback).Run(service::EffectStatus::kUnavailable, nullptr);
}

void FakeProfilePlatformAdapter::WriteAuthorizationCode(
    const std::string &flow_id, const std::vector<uint8_t> &material,
    WriteAuthorizationCodeCallback callback) {
  if (!seal_answers_) {
    std::move(callback).Run(service::EffectStatus::kUnavailable, std::nullopt);
    return;
  }
  sealed_codes_.push_back(
      {flow_id, std::string(material.begin(), material.end())});
  std::move(callback).Run(
      service::EffectStatus::kCompleted,
      "sealed-" + base::NumberToString(++next_handle_));
}

void FakeProfilePlatformAdapter::ConsumeTransient(
    const std::string &secret_handle, ConsumeTransientCallback callback) {
  std::move(callback).Run(service::EffectStatus::kUnavailable, nullptr);
}

void FakeProfilePlatformAdapter::DeleteSecret(
    service::DeleteSecretHandleRequestPtr request,
    DeleteSecretCallback callback) {
  if (request) {
    deleted_handles_.push_back(request->secret_handle);
  }
  std::move(callback).Run(service::EffectStatus::kCompleted, nullptr);
}

void FakeProfilePlatformAdapter::ClearTransient(
    ClearTransientCallback callback) {
  std::move(callback).Run(service::EffectStatus::kCompleted, 0u);
}

void FakeProfilePlatformAdapter::OpenOAuthSurface(
    account_mojom::ResolvedOAuthSurfacePlanPtr plan,
    OpenOAuthSurfaceCallback callback) {
  if (plan) {
    opened_urls_.push_back(plan->authorization_url);
  }
  auto result = service::OAuthSurfaceResult::New();
  result->opened = surface_opens_;
  std::move(callback).Run(service::EffectStatus::kCompleted,
                          std::move(result));
}

void FakeProfilePlatformAdapter::OpenNativeCredentialSurface(
    account_mojom::ResolvedNativeCredentialSurfacePlanPtr plan,
    OpenNativeCredentialSurfaceCallback callback) {
  // InlinedStructPtr does not construct from nullptr.
  std::move(callback).Run(service::EffectStatus::kUnavailable,
                          service::NativeCredentialSurfaceResultPtr());
}

void FakeProfilePlatformAdapter::RotateSession(
    account_mojom::SessionMaterialWriteRequestPtr request,
    RotateSessionCallback callback) {
  std::move(callback).Run(service::EffectStatus::kUnavailable, nullptr);
}

void FakeProfilePlatformAdapter::ReadSession(
    account_mojom::SessionMaterialReadRequestPtr request,
    ReadSessionCallback callback) {
  std::move(callback).Run(service::EffectStatus::kUnavailable, nullptr);
}

void FakeProfilePlatformAdapter::ReadCurrentSession(
    const std::string &session_handle, ReadCurrentSessionCallback callback) {
  std::move(callback).Run(service::EffectStatus::kUnavailable, nullptr);
}

void FakeProfilePlatformAdapter::DeleteSession(
    const std::string &session_handle, DeleteSessionCallback callback) {
  std::move(callback).Run(service::EffectStatus::kUnavailable, false);
}

void FakeProfilePlatformAdapter::ClearSession(ClearSessionCallback callback) {
  std::move(callback).Run(service::EffectStatus::kUnavailable, false);
}

void FakeProfilePlatformAdapter::InspectCurrentSession(
    InspectCurrentSessionCallback callback) {
  std::move(callback).Run(service::EffectStatus::kUnavailable, nullptr);
}

void FakeProfilePlatformAdapter::DeleteSessionVersion(
    account_mojom::SessionMaterialReadRequestPtr request,
    DeleteSessionVersionCallback callback) {
  std::move(callback).Run(service::EffectStatus::kUnavailable, false);
}

void FakeProfilePlatformAdapter::ResolveProviderCredential(
    const std::string &provider_id, const std::string &credential_handle,
    ResolveProviderCredentialCallback callback) {
  std::move(callback).Run(service::EffectStatus::kUnavailable, nullptr);
}

void FakeProfilePlatformAdapter::StoreProviderOauthRecord(
    const std::string &provider_id,
    account_mojom::ProviderOauthRecordPtr record, bool rotation,
    StoreProviderOauthRecordCallback callback) {
  if (!store_answers_) {
    std::move(callback).Run(service::EffectStatus::kUnavailable, false);
    return;
  }
  stored_records_.push_back({provider_id, std::move(record), rotation});
  std::move(callback).Run(service::EffectStatus::kCompleted, true);
}

void FakeProfilePlatformAdapter::ResolveProviderAccess(
    const std::string &provider_id, const std::string &credential_handle,
    ResolveProviderAccessCallback callback) {
  if (access_results_.empty()) {
    std::move(callback).Run(service::EffectStatus::kCompleted, nullptr);
    return;
  }
  account_mojom::ProviderAccessResultPtr result =
      std::move(access_results_.front());
  access_results_.erase(access_results_.begin());
  std::move(callback).Run(service::EffectStatus::kCompleted,
                          std::move(result));
}

void FakeProfilePlatformAdapter::StoreProviderApiKey(
    const std::string &provider_id, const std::vector<uint8_t> &key,
    StoreProviderApiKeyCallback callback) {
  if (!store_answers_) {
    std::move(callback).Run(service::EffectStatus::kUnavailable, false);
    return;
  }
  stored_keys_.push_back({provider_id, std::string(key.begin(), key.end())});
  std::move(callback).Run(service::EffectStatus::kCompleted, true);
}

void FakeProfilePlatformAdapter::OnProviderFlowEvent(
    account_mojom::ProviderFlowEventPtr event,
    OnProviderFlowEventCallback callback) {
  events_.push_back(std::move(event));
  std::move(callback).Run(service::EffectStatus::kCompleted);
}

}  // namespace taffy

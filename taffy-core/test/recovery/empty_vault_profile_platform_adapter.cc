// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/test/recovery/empty_vault_profile_platform_adapter.h"

#include <optional>
#include <utility>

#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"

namespace taffy::test {

namespace account = browser::account::mojom;
namespace service = core_service::mojom;

EmptyVaultProfilePlatformAdapter::EmptyVaultProfilePlatformAdapter() = default;
EmptyVaultProfilePlatformAdapter::~EmptyVaultProfilePlatformAdapter() = default;

mojo::PendingRemote<account::TaffyProfilePlatformAdapter>
EmptyVaultProfilePlatformAdapter::BindNewPipeAndPassRemote() {
  return receiver_.BindNewPipeAndPassRemote();
}

void EmptyVaultProfilePlatformAdapter::GenerateEntropy(
    service::GenerateEntropyRequestPtr,
    GenerateEntropyCallback callback) {
  std::move(callback).Run(service::EffectStatus::kUnavailable, nullptr);
}

void EmptyVaultProfilePlatformAdapter::WriteTransient(
    service::WriteTransientSecretRequestPtr,
    WriteTransientCallback callback) {
  std::move(callback).Run(service::EffectStatus::kUnavailable, nullptr);
}

void EmptyVaultProfilePlatformAdapter::WriteAuthorizationCode(
    const std::string&,
    const std::vector<uint8_t>&,
    WriteAuthorizationCodeCallback callback) {
  std::move(callback).Run(service::EffectStatus::kUnavailable, std::nullopt);
}

void EmptyVaultProfilePlatformAdapter::ConsumeTransient(
    const std::string&,
    ConsumeTransientCallback callback) {
  std::move(callback).Run(service::EffectStatus::kUnavailable, nullptr);
}

void EmptyVaultProfilePlatformAdapter::DeleteSecret(
    service::DeleteSecretHandleRequestPtr,
    DeleteSecretCallback callback) {
  std::move(callback).Run(service::EffectStatus::kUnavailable, nullptr);
}

void EmptyVaultProfilePlatformAdapter::ClearTransient(
    ClearTransientCallback callback) {
  std::move(callback).Run(service::EffectStatus::kCompleted, 0u);
}

void EmptyVaultProfilePlatformAdapter::OpenOAuthSurface(
    account::ResolvedOAuthSurfacePlanPtr,
    OpenOAuthSurfaceCallback callback) {
  std::move(callback).Run(service::EffectStatus::kUnavailable, nullptr);
}

void EmptyVaultProfilePlatformAdapter::OpenNativeCredentialSurface(
    account::ResolvedNativeCredentialSurfacePlanPtr,
    OpenNativeCredentialSurfaceCallback callback) {
  std::move(callback).Run(service::EffectStatus::kUnavailable,
                          service::NativeCredentialSurfaceResultPtr());
}

void EmptyVaultProfilePlatformAdapter::RotateSession(
    account::SessionMaterialWriteRequestPtr,
    RotateSessionCallback callback) {
  std::move(callback).Run(service::EffectStatus::kUnavailable, nullptr);
}

void EmptyVaultProfilePlatformAdapter::ReadSession(
    account::SessionMaterialReadRequestPtr,
    ReadSessionCallback callback) {
  std::move(callback).Run(service::EffectStatus::kUnavailable, nullptr);
}

void EmptyVaultProfilePlatformAdapter::ReadCurrentSession(
    const std::string&,
    ReadCurrentSessionCallback callback) {
  std::move(callback).Run(service::EffectStatus::kUnavailable, nullptr);
}

void EmptyVaultProfilePlatformAdapter::DeleteSession(
    const std::string&,
    DeleteSessionCallback callback) {
  std::move(callback).Run(service::EffectStatus::kUnavailable, false);
}

void EmptyVaultProfilePlatformAdapter::ClearSession(
    ClearSessionCallback callback) {
  std::move(callback).Run(service::EffectStatus::kCompleted, true);
}

void EmptyVaultProfilePlatformAdapter::InspectCurrentSession(
    InspectCurrentSessionCallback callback) {
  ++inspection_count_;
  std::move(callback).Run(service::EffectStatus::kCompleted, nullptr);
}

void EmptyVaultProfilePlatformAdapter::DeleteSessionVersion(
    account::SessionMaterialReadRequestPtr,
    DeleteSessionVersionCallback callback) {
  std::move(callback).Run(service::EffectStatus::kUnavailable, false);
}

void EmptyVaultProfilePlatformAdapter::ResolveProviderCredential(
    const std::string&,
    const std::string&,
    ResolveProviderCredentialCallback callback) {
  std::move(callback).Run(service::EffectStatus::kUnavailable, nullptr);
}

void EmptyVaultProfilePlatformAdapter::StoreProviderOauthRecord(
    const std::string&,
    account::ProviderOauthRecordPtr,
    bool,
    StoreProviderOauthRecordCallback callback) {
  std::move(callback).Run(service::EffectStatus::kUnavailable, false);
}

void EmptyVaultProfilePlatformAdapter::ResolveProviderAccess(
    const std::string&,
    const std::string&,
    ResolveProviderAccessCallback callback) {
  std::move(callback).Run(service::EffectStatus::kUnavailable, nullptr);
}

void EmptyVaultProfilePlatformAdapter::OnProviderFlowEvent(
    account::ProviderFlowEventPtr,
    OnProviderFlowEventCallback callback) {
  std::move(callback).Run(service::EffectStatus::kUnavailable);
}

void EmptyVaultProfilePlatformAdapter::StoreProviderApiKey(
    const std::string&,
    const std::vector<uint8_t>&,
    StoreProviderApiKeyCallback callback) {
  std::move(callback).Run(service::EffectStatus::kUnavailable, false);
}

}  // namespace taffy::test

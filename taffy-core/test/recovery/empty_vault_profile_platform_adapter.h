// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_TEST_RECOVERY_EMPTY_VAULT_PROFILE_PLATFORM_ADAPTER_H_
#define TAFFY_TEST_RECOVERY_EMPTY_VAULT_PROFILE_PLATFORM_ADAPTER_H_

#include <cstdint>
#include <string>
#include <vector>

#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "taffy/browser/account/profile_platform_adapter.mojom.h"

namespace taffy::test {

// Test-only Android platform boundary for a regular profile whose encrypted
// account vault is empty. ChromeBrowserTestsActivity does not create the
// product Compose owner of this Mojo pipe, so recovery browser tests bind this
// adapter explicitly before asking the real profile manager to bootstrap.
//
// Empty-vault inspection and idempotent clearing succeed. Every operation
// that would create, reveal, or spend secret material remains unavailable.
class EmptyVaultProfilePlatformAdapter final
    : public browser::account::mojom::TaffyProfilePlatformAdapter {
 public:
  EmptyVaultProfilePlatformAdapter();
  EmptyVaultProfilePlatformAdapter(const EmptyVaultProfilePlatformAdapter&) =
      delete;
  EmptyVaultProfilePlatformAdapter& operator=(
      const EmptyVaultProfilePlatformAdapter&) = delete;
  ~EmptyVaultProfilePlatformAdapter() override;

  mojo::PendingRemote<browser::account::mojom::TaffyProfilePlatformAdapter>
  BindNewPipeAndPassRemote();

  uint32_t inspection_count() const { return inspection_count_; }

  // TaffyProfilePlatformAdapter:
  void GenerateEntropy(core_service::mojom::GenerateEntropyRequestPtr request,
                       GenerateEntropyCallback callback) override;
  void WriteTransient(
      core_service::mojom::WriteTransientSecretRequestPtr request,
      WriteTransientCallback callback) override;
  void WriteAuthorizationCode(const std::string& flow_id,
                              const std::vector<uint8_t>& material,
                              WriteAuthorizationCodeCallback callback) override;
  void ConsumeTransient(const std::string& secret_handle,
                        ConsumeTransientCallback callback) override;
  void DeleteSecret(core_service::mojom::DeleteSecretHandleRequestPtr request,
                    DeleteSecretCallback callback) override;
  void ClearTransient(ClearTransientCallback callback) override;
  void OpenOAuthSurface(
      browser::account::mojom::ResolvedOAuthSurfacePlanPtr plan,
      OpenOAuthSurfaceCallback callback) override;
  void OpenNativeCredentialSurface(
      browser::account::mojom::ResolvedNativeCredentialSurfacePlanPtr plan,
      OpenNativeCredentialSurfaceCallback callback) override;
  void RotateSession(
      browser::account::mojom::SessionMaterialWriteRequestPtr request,
      RotateSessionCallback callback) override;
  void ReadSession(
      browser::account::mojom::SessionMaterialReadRequestPtr request,
      ReadSessionCallback callback) override;
  void ReadCurrentSession(const std::string& session_handle,
                          ReadCurrentSessionCallback callback) override;
  void DeleteSession(const std::string& session_handle,
                     DeleteSessionCallback callback) override;
  void ClearSession(ClearSessionCallback callback) override;
  void InspectCurrentSession(InspectCurrentSessionCallback callback) override;
  void DeleteSessionVersion(
      browser::account::mojom::SessionMaterialReadRequestPtr request,
      DeleteSessionVersionCallback callback) override;
  void ResolveProviderCredential(
      const std::string& provider_id,
      const std::string& credential_handle,
      ResolveProviderCredentialCallback callback) override;
  void StoreProviderOauthRecord(
      const std::string& provider_id,
      browser::account::mojom::ProviderOauthRecordPtr record,
      bool rotation,
      StoreProviderOauthRecordCallback callback) override;
  void ResolveProviderAccess(const std::string& provider_id,
                             const std::string& credential_handle,
                             ResolveProviderAccessCallback callback) override;
  void OnProviderFlowEvent(browser::account::mojom::ProviderFlowEventPtr event,
                           OnProviderFlowEventCallback callback) override;
  void StoreProviderApiKey(const std::string& provider_id,
                           const std::vector<uint8_t>& key,
                           StoreProviderApiKeyCallback callback) override;

 private:
  uint32_t inspection_count_ = 0u;
  mojo::Receiver<browser::account::mojom::TaffyProfilePlatformAdapter>
      receiver_{this};
};

}  // namespace taffy::test

#endif  // TAFFY_TEST_RECOVERY_EMPTY_VAULT_PROFILE_PLATFORM_ADAPTER_H_

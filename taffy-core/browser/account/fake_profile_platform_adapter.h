// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_ACCOUNT_FAKE_PROFILE_PLATFORM_ADAPTER_H_
#define TAFFY_BROWSER_ACCOUNT_FAKE_PROFILE_PLATFORM_ADAPTER_H_

#include <optional>
#include <string>
#include <vector>

#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "taffy/browser/account/profile_platform_adapter.mojom.h"

namespace taffy {

// A dictating stand-in for the Android platform adapter, for suites that
// drive the browser side of a provider sign-in or resolve (decision 0081).
// The provider seams record what crossed and answer what a test armed; every
// session and secret seam the account plane owns answers kUnavailable, which
// is the honest posture of a fake that implements no vault. Nothing here is
// compiled into the product.
class FakeProfilePlatformAdapter final
    : public browser::account::mojom::TaffyProfilePlatformAdapter {
 public:
  struct SealedCode {
    std::string flow_id;
    std::string material;
  };
  struct StoredRecord {
    std::string provider_id;
    browser::account::mojom::ProviderOauthRecordPtr record;
    bool rotation = false;
  };
  struct StoredKey {
    std::string provider_id;
    std::string key;
  };

  FakeProfilePlatformAdapter();
  FakeProfilePlatformAdapter(const FakeProfilePlatformAdapter &) = delete;
  FakeProfilePlatformAdapter &operator=(const FakeProfilePlatformAdapter &) =
      delete;
  ~FakeProfilePlatformAdapter() override;

  mojo::PendingRemote<browser::account::mojom::TaffyProfilePlatformAdapter>
  BindNewPipeAndPassRemote();

  // Knobs a test arms before driving the broker.
  void set_surface_opens(bool opens) { surface_opens_ = opens; }
  void set_seal_answers(bool answers) { seal_answers_ = answers; }
  void set_store_answers(bool answers) { store_answers_ = answers; }
  // Queued answers for ResolveProviderAccess, consumed front-first; an empty
  // queue answers a null result, the not-configured shape.
  void EnqueueAccessResult(
      browser::account::mojom::ProviderAccessResultPtr result);

  // What crossed the seam, in order.
  const std::vector<std::string> &opened_urls() const { return opened_urls_; }
  const std::vector<SealedCode> &sealed_codes() const { return sealed_codes_; }
  const std::vector<std::string> &deleted_handles() const {
    return deleted_handles_;
  }
  const std::vector<StoredRecord> &stored_records() const {
    return stored_records_;
  }
  const std::vector<StoredKey> &stored_keys() const { return stored_keys_; }
  const std::vector<browser::account::mojom::ProviderFlowEventPtr> &events()
      const {
    return events_;
  }

  // TaffyProfilePlatformAdapter:
  void GenerateEntropy(core_service::mojom::GenerateEntropyRequestPtr request,
                       GenerateEntropyCallback callback) override;
  void WriteTransient(
      core_service::mojom::WriteTransientSecretRequestPtr request,
      WriteTransientCallback callback) override;
  void WriteAuthorizationCode(const std::string &flow_id,
                              const std::vector<uint8_t> &material,
                              WriteAuthorizationCodeCallback callback) override;
  void ConsumeTransient(const std::string &secret_handle,
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
  void ReadCurrentSession(const std::string &session_handle,
                          ReadCurrentSessionCallback callback) override;
  void DeleteSession(const std::string &session_handle,
                     DeleteSessionCallback callback) override;
  void ClearSession(ClearSessionCallback callback) override;
  void InspectCurrentSession(InspectCurrentSessionCallback callback) override;
  void DeleteSessionVersion(
      browser::account::mojom::SessionMaterialReadRequestPtr request,
      DeleteSessionVersionCallback callback) override;
  void ResolveProviderCredential(
      const std::string &provider_id, const std::string &credential_handle,
      ResolveProviderCredentialCallback callback) override;
  void StoreProviderOauthRecord(
      const std::string &provider_id,
      browser::account::mojom::ProviderOauthRecordPtr record, bool rotation,
      StoreProviderOauthRecordCallback callback) override;
  void ResolveProviderAccess(const std::string &provider_id,
                             const std::string &credential_handle,
                             ResolveProviderAccessCallback callback) override;
  void OnProviderFlowEvent(browser::account::mojom::ProviderFlowEventPtr event,
                           OnProviderFlowEventCallback callback) override;
  void StoreProviderApiKey(const std::string &provider_id,
                           const std::vector<uint8_t> &key,
                           StoreProviderApiKeyCallback callback) override;

 private:
  bool surface_opens_ = true;
  bool seal_answers_ = true;
  bool store_answers_ = true;
  int next_handle_ = 0;
  std::vector<std::string> opened_urls_;
  std::vector<SealedCode> sealed_codes_;
  std::vector<std::string> deleted_handles_;
  std::vector<StoredRecord> stored_records_;
  std::vector<StoredKey> stored_keys_;
  std::vector<browser::account::mojom::ProviderFlowEventPtr> events_;
  std::vector<browser::account::mojom::ProviderAccessResultPtr>
      access_results_;
  mojo::Receiver<browser::account::mojom::TaffyProfilePlatformAdapter>
      receiver_{this};
};

}  // namespace taffy

#endif  // TAFFY_BROWSER_ACCOUNT_FAKE_PROFILE_PLATFORM_ADAPTER_H_

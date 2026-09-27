// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_ACCOUNT_PROFILE_ACCOUNT_BROKER_H_
#define TAFFY_BROWSER_ACCOUNT_PROFILE_ACCOUNT_BROKER_H_

#include <stdint.h>

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/containers/flat_map.h"
#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/sequence_checker.h"
#include "base/timer/timer.h"
#include "base/values.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "taffy/browser/account/profile_platform_adapter.mojom.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"
#include "url/gurl.h"

namespace content {
class BrowserContext;
} // namespace content

namespace network {
class SharedURLLoaderFactory;
class SimpleURLLoader;
} // namespace network

namespace taffy {

// Profile browser broker for the portable Rust account reducer. It owns fixed
// provider routes, callback correlation, network traffic, and opaque platform
// handles; none of those facts cross into UI, renderer, core, or workers.
class ProfileAccountBroker final {
public:
  using EffectCallback =
      base::OnceCallback<void(core_service::mojom::EffectResultPtr)>;
  using RedirectSubmissionCallback = base::OnceCallback<void(bool)>;
  using RedirectCallback = base::OnceCallback<void(
      uint64_t, core_service::mojom::AuthCallbackCommandPtr,
      RedirectSubmissionCallback)>;
  using TokenValidationCallback = base::OnceCallback<void(
      core_service::mojom::AccountTokenValidationResultPtr)>;
  using TokenValidator = base::RepeatingCallback<void(
      core_service::mojom::AccountTokenValidationRequestPtr,
      TokenValidationCallback)>;
  using FlowTerminalCallback = base::RepeatingCallback<void(
      uint64_t, core_service::mojom::AuthCallbackCommandPtr)>;
  using SessionInspectionCallback =
      base::OnceCallback<void(bool, std::optional<std::string>, uint64_t)>;

  explicit ProfileAccountBroker(content::BrowserContext *browser_context);
  ProfileAccountBroker(const ProfileAccountBroker &) = delete;
  ProfileAccountBroker &operator=(const ProfileAccountBroker &) = delete;
  ~ProfileAccountBroker();

  void BindPlatformAdapter(
      mojo::PendingRemote<browser::account::mojom::TaffyProfilePlatformAdapter>
          adapter);
  void SetTokenValidator(TokenValidator validator);
  void SetFlowTerminalCallback(FlowTerminalCallback callback);
  void CancelGeneration(uint64_t generation);
  // True while an account redirect remains bound to a live Core generation.
  // This browser-held leg cannot be reconstructed after idle teardown.
  bool has_pending_auth_flow() const;
  void InspectCanonicalSession(SessionInspectionCallback callback);
  void ClearCanonicalSession(base::OnceCallback<void(bool)> callback);

  // Opens the sealed provider credential the model broker asked for.
  //
  // The material never enters the sandbox: this is the browser-process half of
  // decision 0049. The callback runs exactly once with the opened bytes, or
  // with nullopt when the adapter is unbound, the profile is private, or the
  // store had nothing usable. The model broker maps nullopt to denied.
  void ResolveProviderCredential(
      const std::string &provider_id, const std::string &credential_handle,
      base::OnceCallback<void(std::optional<std::string>)> callback);

  // The third canonical-session read path (decision 0082): the GoTrue access
  // token alone, for the browser's own entitlement mint. Everything else the
  // sealed payload holds — the refresh token above all — is zeroed before the
  // callback runs, and the token never enters the sandbox, a contract struct,
  // or a log. The callback runs exactly once; nullopt is "no usable session",
  // which the mint maps to a transport failure rather than a definitive
  // absence.
  void ReadCanonicalAccessToken(
      base::OnceCallback<void(std::optional<std::string>)> callback);

  // The provider-subscription seams (decision 0081), thin passthroughs over
  // the one Android platform adapter this class owns. The provider auth
  // broker performs the vendor's network legs and comes here for everything
  // Android holds: the Custom Tab, the one-shot vault, the sealed record
  // store and the visible flow state. Implemented in
  // profile_account_broker_provider.cc.
  void OpenProviderAuthSurface(const std::string &flow_id,
                               const std::string &authorization_url,
                               base::OnceCallback<void(bool)> callback);
  void SealProviderAuthorizationCode(
      const std::string &flow_id, std::string code,
      base::OnceCallback<void(std::optional<std::string>)> callback);
  void DeleteProviderTransient(const std::string &handle);
  void StoreProviderOauthRecord(
      const std::string &provider_id,
      browser::account::mojom::ProviderOauthRecordPtr record, bool rotation,
      base::OnceCallback<void(bool)> callback);
  // The key half of the same seam: a durable key this browser's own sign-in
  // minted, filed exactly where a pasted one is filed. The key is zeroed in
  // this process the moment it has been handed across.
  void StoreProviderApiKey(const std::string &provider_id, std::string key,
                           base::OnceCallback<void(bool)> callback);
  void NotifyProviderFlowEvent(
      browser::account::mojom::ProviderFlowEventPtr event);
  void ResolveProviderAccess(
      const std::string &provider_id, const std::string &credential_handle,
      base::OnceCallback<void(browser::account::mojom::ProviderAccessResultPtr)>
          callback);

  // Spends one one-shot transient from the platform vault (decision 0083):
  // the key probe's pasted draft, minted Kotlin-side and consumed exactly
  // once here. The callback runs exactly once; nullopt is "absent or already
  // spent", which the model broker maps to unavailable so the provider is
  // honestly reported unreached rather than judged. The material is zeroed
  // in this process the moment it is copied out.
  void ConsumeTransientCredential(
      const std::string &credential_handle,
      base::OnceCallback<void(std::optional<std::string>)> callback);

  void DispatchSecureStore(core_service::mojom::EffectEnvelopePtr effect,
                           EffectCallback callback);
  void DispatchAuthSurface(core_service::mojom::EffectEnvelopePtr effect,
                           EffectCallback callback);
  void DispatchNetwork(core_service::mojom::EffectEnvelopePtr effect,
                       EffectCallback callback);

  // Returns true only after a structurally valid callback has atomically
  // claimed one pending browser binding. The callback may complete later when
  // Android has sealed the authorization code behind a one-shot handle.
  bool DeliverAuthCallback(std::string raw_uri, RedirectCallback callback);

private:
  struct PendingOAuthBinding {
    std::string flow_id;
    std::string redirect_binding_id;
    std::string pkce_verifier_handle;
    uint64_t service_generation = 0;
    uint64_t deadline_monotonic_ms = 0;
  };

  // What the account plane says this account is called. Account metadata, not
  // credential material, and deliberately not sealed with the tokens: it has
  // exactly one durable home, the journal row that already holds the account
  // subject and the method, and a second copy inside the vault blob would be a
  // second thing to keep in step for no reader. It travels through this class
  // only to reach the receipt that commits it there.
  struct AccountIdentity {
    std::optional<std::string> email;
    std::optional<std::string> display_name;
  };

  struct SessionPayload {
    std::string access_token;
    std::string refresh_token;
    std::string account_subject;
    core_service::mojom::AccountAuthMethod auth_method =
        core_service::mojom::AccountAuthMethod::kGoogle;

    SessionPayload();
    SessionPayload(SessionPayload &&) noexcept;
    SessionPayload &operator=(SessionPayload &&) noexcept;
    ~SessionPayload();
  };

  struct NetworkResponse {
    int net_error = 0;
    int http_status = 0;
    std::optional<std::string> body;
    bool dispatched = false;
  };

  enum class AccountEndpointKind {
    kPkceToken,
    kNativeToken,
    kRefreshToken,
    kLogout,
    kEmailOtp,
  };

  using NetworkResponseCallback = base::OnceCallback<void(NetworkResponse)>;

  core_service::mojom::EffectResultPtr
  MakeResult(const core_service::mojom::EffectEnvelope &effect,
             core_service::mojom::EffectStatus status) const;
  void OnGeneratedEntropy(core_service::mojom::EffectEnvelopePtr effect,
                          EffectCallback callback,
                          core_service::mojom::EffectStatus status,
                          core_service::mojom::GeneratedEntropyResultPtr body);
  void
  OnTransientWritten(core_service::mojom::EffectEnvelopePtr effect,
                     EffectCallback callback,
                     core_service::mojom::EffectStatus status,
                     core_service::mojom::TransientSecretWriteResultPtr body);
  void OnSecretDeleted(core_service::mojom::EffectEnvelopePtr effect,
                       EffectCallback callback,
                       core_service::mojom::EffectStatus status,
                       core_service::mojom::DeletedSecretHandleResultPtr body);
  void OnOAuthSurfaceOpened(core_service::mojom::EffectEnvelopePtr effect,
                            std::string state, EffectCallback callback,
                            core_service::mojom::EffectStatus status,
                            core_service::mojom::OAuthSurfaceResultPtr body);
  void OnNativeSurfaceOpened(
      core_service::mojom::EffectEnvelopePtr effect, EffectCallback callback,
      core_service::mojom::EffectStatus status,
      core_service::mojom::NativeCredentialSurfaceResultPtr body);
  void OnAuthorizationCodeStored(
      PendingOAuthBinding binding, std::string returned_state,
      RedirectCallback callback, core_service::mojom::EffectStatus status,
      const std::optional<std::string> &secret_handle);
  void OnAuthCallbackSubmitted(std::string authorization_code_handle,
                               std::string pkce_verifier_handle,
                               bool submitted);

  std::optional<std::string> BuildAuthorizationUrl(
      const core_service::mojom::OAuthSurfaceRequest &request) const;
  void PruneExpiredBindings(uint64_t now_monotonic_ms);
  void ScheduleBindingExpiry(uint64_t now_monotonic_ms);
  void OnBindingExpiry();
  void OnAuthorizationCodeConsumed(
      core_service::mojom::EffectEnvelopePtr effect, EffectCallback callback,
      core_service::mojom::EffectStatus status,
      browser::account::mojom::SecretMaterialReadResultPtr result);
  void OnVerifierConsumed(
      core_service::mojom::EffectEnvelopePtr effect, EffectCallback callback,
      std::string authorization_code, core_service::mojom::EffectStatus status,
      browser::account::mojom::SecretMaterialReadResultPtr result);
  void OnNativeCredentialConsumed(
      core_service::mojom::EffectEnvelopePtr effect, EffectCallback callback,
      core_service::mojom::EffectStatus status,
      browser::account::mojom::SecretMaterialReadResultPtr result);
  void OnNativeRawNonceConsumed(
      core_service::mojom::EffectEnvelopePtr effect, EffectCallback callback,
      std::string credential, core_service::mojom::EffectStatus status,
      browser::account::mojom::SecretMaterialReadResultPtr result);
  void OnRefreshSessionRead(
      core_service::mojom::EffectEnvelopePtr effect, EffectCallback callback,
      core_service::mojom::EffectStatus status,
      browser::account::mojom::SecretMaterialReadResultPtr result);
  void OnCurrentSessionReadForRevoke(
      core_service::mojom::EffectEnvelopePtr effect, EffectCallback callback,
      core_service::mojom::EffectStatus status,
      browser::account::mojom::SessionMaterialReadResultPtr result);
  void StartTokenRequest(core_service::mojom::EffectEnvelopePtr effect,
                         EffectCallback callback, std::string request_body,
                         core_service::mojom::AccountAuthMethod method,
                         std::optional<std::string> previous_handle,
                         uint64_t target_rotation,
                         std::optional<std::string> expected_subject);
  void OnTokenResponse(core_service::mojom::EffectEnvelopePtr effect,
                       EffectCallback callback,
                       core_service::mojom::AccountAuthMethod method,
                       std::optional<std::string> previous_handle,
                       uint64_t target_rotation,
                       std::optional<std::string> expected_subject,
                       NetworkResponse response);
  void OnTokenValidated(
      core_service::mojom::EffectEnvelopePtr effect, EffectCallback callback,
      core_service::mojom::AccountAuthMethod method,
      std::optional<std::string> previous_handle, uint64_t target_rotation,
      std::optional<std::string> expected_subject,
      core_service::mojom::AccountTokenValidationResultPtr result);
  void OnSessionRotated(
      core_service::mojom::EffectEnvelopePtr effect, EffectCallback callback,
      std::string account_subject,
      core_service::mojom::AccountAuthMethod auth_method,
      uint64_t expires_at_monotonic_ms, AccountIdentity identity,
      core_service::mojom::EffectStatus status,
      browser::account::mojom::SessionMaterialWriteResultPtr result);
  void DeleteExactSessionAndFinish(
      core_service::mojom::EffectEnvelopePtr effect, EffectCallback callback,
      std::string session_handle, uint64_t expected_rotation,
      core_service::mojom::EffectStatus terminal_status);
  void OnExactSessionDeleted(core_service::mojom::EffectEnvelopePtr effect,
                             EffectCallback callback,
                             core_service::mojom::EffectStatus terminal_status,
                             core_service::mojom::EffectStatus storage_status,
                             bool deleted);
  void OnRevokeResponse(core_service::mojom::EffectEnvelopePtr effect,
                        EffectCallback callback, NetworkResponse response);
  void StartEmailLinkRequest(core_service::mojom::EffectEnvelopePtr effect,
                             EffectCallback callback);
  void OnEmailLinkResponse(core_service::mojom::EffectEnvelopePtr effect,
                           EffectCallback callback, NetworkResponse response);

  bool StartJsonRequest(const core_service::mojom::EffectEnvelope &effect,
                        const GURL &endpoint, std::string request_body,
                        std::optional<std::string> bearer_token,
                        NetworkResponseCallback callback);
  void OnNetworkComplete(std::string effect_id,
                         NetworkResponseCallback callback,
                         std::optional<std::string> response_body);
  void
  DeleteSessionAndFinish(core_service::mojom::EffectEnvelopePtr effect,
                         EffectCallback callback,
                         core_service::mojom::EffectStatus terminal_status);
  void OnCanonicalSessionCleared(
      core_service::mojom::EffectEnvelopePtr effect, EffectCallback callback,
      core_service::mojom::EffectStatus terminal_status,
      core_service::mojom::EffectStatus storage_status, bool cleared);
  void OnCanonicalSessionInspected(
      SessionInspectionCallback callback,
      core_service::mojom::EffectStatus status,
      browser::account::mojom::SessionMaterialMetadataPtr metadata);

  void OnAccessTokenSessionInspected(
      base::OnceCallback<void(std::optional<std::string>)> callback,
      core_service::mojom::EffectStatus status,
      browser::account::mojom::SessionMaterialMetadataPtr metadata);
  void OnAccessTokenSessionRead(
      base::OnceCallback<void(std::optional<std::string>)> callback,
      core_service::mojom::EffectStatus status,
      browser::account::mojom::SessionMaterialReadResultPtr result);
  void OnProviderCredentialResolved(
      base::OnceCallback<void(std::optional<std::string>)> callback,
      core_service::mojom::EffectStatus status,
      browser::account::mojom::SecretMaterialReadResultPtr result);
  bool AcceptGeneration(uint64_t generation);
  void InstallPlatformDisconnectHandler();
  void CancelNetworkRequests();
  void
  SettleOAuthBindings(core_service::mojom::AuthCallbackStatus terminal_status);
  void DeleteTransientHandlesForFailedEffect(
      const core_service::mojom::EffectEnvelope &effect);
  void DeleteTransientBestEffort(std::string secret_handle);

  std::optional<GURL> AccountEndpoint(AccountEndpointKind kind) const;
  std::optional<SessionPayload>
  DecodeSessionPayload(const std::vector<uint8_t> &bytes) const;
  std::optional<std::vector<uint8_t>>
  EncodeSessionPayload(const SessionPayload &payload) const;

  const raw_ptr<content::BrowserContext> browser_context_;
  const bool private_profile_;
  scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory_;
  mojo::Remote<browser::account::mojom::TaffyProfilePlatformAdapter>
      platform_adapter_;
  TokenValidator token_validator_;
  FlowTerminalCallback flow_terminal_callback_;
  uint64_t active_generation_ = 0;
  base::flat_map<std::string, PendingOAuthBinding> oauth_bindings_by_state_;
  base::OneShotTimer oauth_binding_timer_;
  base::flat_map<std::string, std::unique_ptr<network::SimpleURLLoader>>
      network_loaders_;

  SEQUENCE_CHECKER(sequence_checker_);
  base::WeakPtrFactory<ProfileAccountBroker> weak_factory_{this};
};

} // namespace taffy

#endif // TAFFY_BROWSER_ACCOUNT_PROFILE_ACCOUNT_BROKER_H_

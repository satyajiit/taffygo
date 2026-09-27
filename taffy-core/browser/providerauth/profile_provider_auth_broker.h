// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_PROVIDERAUTH_PROFILE_PROVIDER_AUTH_BROKER_H_
#define TAFFY_BROWSER_PROVIDERAUTH_PROFILE_PROVIDER_AUTH_BROKER_H_

#include <stdint.h>

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/sequence_checker.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "taffy/browser/account/profile_platform_adapter.mojom.h"
#include "taffy/browser/providerauth/provider_auth_configuration.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"

class GURL;

namespace network {
class SharedURLLoaderFactory;
class SimpleURLLoader;
}  // namespace network

namespace taffy {

class ProfileAccountBroker;

// Runs every network leg of a provider subscription sign-in (decision 0081):
// device authorization and its token poll, the PKCE exchange behind the
// Custom Tab, the second exchange a few vendors require after the first, the
// one refresh leg, and best-effort revocation on sign-out. The core admits
// and records each flow; Android renders it and seals every token — this
// class is the only place the vendor's OAuth wire shapes exist, and nothing
// it holds survives into logs, UI state, or the portable protocol.
//
// A PKCE flow's code comes back one of two ways, and both end here
// (decision 0095 section 2). Either the browser intercepts the navigation to
// the borrowed client's registered redirect and cancels it before it
// connects, or the person reads the code the vendor displayed and enters it
// on the product's own chrome. Both are claimed by the same constant-time
// comparison against a live flow, because a second claim path would be a
// second place to get that comparison wrong.
class ProfileProviderAuthBroker final {
 public:
  // Delivers one flow terminal for the manager to wrap into a
  // PROVIDER_AUTH_CALLBACK command. The boolean answer says whether the core
  // accepted it; a refused terminal ends the browser flow all the same.
  using FlowTerminalCallback = base::RepeatingCallback<void(
      uint64_t /*service_generation*/,
      core_service::mojom::ProviderAuthCallbackCommandPtr,
      base::OnceCallback<void(bool)>)>;

  // Answers one refresh leg: the rotated record on success, or null with
  // `definitive` saying whether the grant itself is dead (invalid_grant) or
  // the failure was transient.
  using RefreshCallback = base::OnceCallback<void(
      browser::account::mojom::ProviderOauthRecordPtr,
      bool /*definitive*/)>;

  // The loader factory is injected rather than pulled off a BrowserContext,
  // like the model broker's: the factory site owns the profile dance, and a
  // test dictates every vendor answer through a fake.
  // Null refuses every network leg with a terminal, crashing nothing.
  ProfileProviderAuthBroker(
      scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory,
      ProfileAccountBroker *account_broker);
  ProfileProviderAuthBroker(const ProfileProviderAuthBroker &) = delete;
  ProfileProviderAuthBroker &operator=(const ProfileProviderAuthBroker &) =
      delete;
  ~ProfileProviderAuthBroker();

  void SetFlowTerminalCallback(FlowTerminalCallback callback);

  // Starts the flow the core just admitted. Every exit — success, denial,
  // vendor error, deadline, cancellation, or a vendor with no dated terms
  // review — delivers exactly one terminal through the callback above, so
  // the core's pending marker is never left standing.
  void StartFlow(const std::string &provider_id, const std::string &flow_id,
                 const std::string &redirect_binding_id,
                 uint64_t service_generation);

  // Routes one custom-scheme provider redirect. Returns whether a pending
  // PKCE flow's state claimed it; an unclaimed redirect is dropped by the
  // caller. Reserved for a registration of the product's own: no compiled row
  // presents the product's callback today, so nothing claims here yet.
  bool DeliverProviderCallback(std::string raw_uri);

  // Offers one navigation to the flows in progress. Returns whether it is a
  // redirect a running flow was waiting for — in which case the code and
  // state have already been taken from it and the caller must cancel the
  // navigation, so that nothing connects and no page ever sees the code.
  //
  // Matching is against the address the running flow actually presented, not
  // against the vendor table: an address that belongs to some other row is
  // an ordinary navigation, and a person who types one deserves to arrive.
  bool ClaimInterceptedRedirect(const GURL &url);

  // Whether any running flow presented an address the navigation interceptor
  // could claim. Used only to avoid allocating a throttle for ordinary page
  // loads; the exact address, state and code are still checked at request time.
  bool HasInterceptableRedirect() const;

  // True while a provider sign-in still belongs to a Core generation. This
  // browser-held OAuth leg cannot be reconstructed after idle teardown.
  bool has_active_flows() const;

  // Accepts a code a person read off the vendor's page and typed on trusted
  // chrome — the fallback for every flow whose redirect the product could
  // not intercept. `entered` is what the vendor displayed, which for some
  // vendors is the code and the state joined by `#`. Returns whether a live
  // flow accepted it; a wrong or stale entry changes nothing, so a person
  // can simply try again until the flow's deadline.
  bool SubmitManualCode(const std::string &flow_id, std::string entered);

  // Ends one running flow because the person said so. Returns whether there
  // was one to end. A cancelled flow files the same terminal a dismissed
  // authorization does, because that is what it is: the person decided not
  // to connect, and the roster must look exactly as it did before.
  bool CancelFlow(const std::string &flow_id);

  // Stops only the flows owned by a lost Core Service generation. There is no
  // terminal to send to that dead generation; each exact Android observer is
  // told the flow became unavailable so no surface can remain attached to
  // native work whose portable pending record is gone.
  void AbortGeneration(uint64_t service_generation);

  // The one refresh leg, single-flight per provider: concurrent callers for
  // the same provider join the flight in progress and share its answer.
  void RefreshCredential(const std::string &provider_id,
                         std::string refresh_token, RefreshCallback callback);

  // Posts the vendor's revocation endpoint on sign-out, where one exists.
  // Fire-and-forget by design: the forget proceeds whatever this answers,
  // because the person's device is theirs to clear.
  void RevokeBestEffort(const std::string &provider_id, std::string token);

  // Ends every running flow and refresh without terminals; shutdown only.
  void CancelAll();

 private:
  struct PendingFlow {
    std::string provider_id;
    std::string flow_id;
    std::string redirect_binding_id;
    uint64_t service_generation = 0;
    // The browser-minted flow nonce, base64url. It is what the terminal
    // echoes to the core, and it is independent entropy for every vendor
    // without exception — see `authorization_state` for why that matters.
    std::string state;
    // What was sent as the OAuth `state` parameter, and therefore the only
    // value a redirect may claim this flow with. Equal to `state` for every
    // vendor but the one whose row says it wants the PKCE verifier there.
    std::string authorization_state;
    // PKCE only.
    std::string pkce_verifier;
    // The redirect address this flow presented, empty when it presented
    // none. The interceptor matches this rather than the vendor table, so a
    // flow can only ever be claimed by the address it actually asked for.
    std::string redirect_uri;
    // Device only: the grant credential the token poll spends.
    std::string device_code;
    base::TimeDelta poll_interval;
    std::unique_ptr<network::SimpleURLLoader> loader;
    std::unique_ptr<base::OneShotTimer> poll_timer;
    std::unique_ptr<base::OneShotTimer> deadline_timer;
  };

  struct RefreshFlight {
    std::unique_ptr<network::SimpleURLLoader> loader;
    std::vector<RefreshCallback> callbacks;
  };

  // Flow lifecycle and the one claim path (profile_provider_auth_broker.cc).
  bool ClaimRedirect(const std::string &state, std::optional<std::string> code,
                     bool denied);
  void OnRedirectCode(const std::string &flow_id, std::string code);
  void OnDeadline(const std::string &flow_id);

  // Device legs (profile_provider_auth_broker_device.cc).
  void StartDeviceFlow(PendingFlow &flow, const ProviderAuthVendor &vendor);
  void OnDeviceCodeResponse(const std::string &flow_id,
                            std::optional<std::string> body);
  void PollDeviceToken(const std::string &flow_id);
  void OnDeviceTokenResponse(const std::string &flow_id,
                             std::optional<std::string> body);

  // PKCE legs (profile_provider_auth_broker_pkce.cc).
  void StartPkceFlow(PendingFlow &flow, const ProviderAuthVendor &vendor);
  void OnAuthSurfaceOpened(const std::string &flow_id, bool opened);
  void OnCodeSealed(const std::string &flow_id, std::string code,
                    std::optional<std::string> handle);
  void OnTerminalSubmitted(const std::string &flow_id, std::string code,
                           bool accepted);
  void OnExchangeResponse(const std::string &flow_id,
                          std::optional<std::string> body);

  // Vendor token answers (profile_provider_auth_broker_tokens.cc).
  void StoreTokens(const std::string &flow_id, const std::string &body);
  void StoreRecord(const std::string &flow_id,
                   browser::account::mojom::ProviderOauthRecordPtr record);
  void StoreMintedKey(const std::string &flow_id, const std::string &body);
  void OnMintedKeyStored(const std::string &provider_id,
                         const std::string &flow_id, bool stored);
  void ExchangeSecondaryToken(const std::string &flow_id,
                              const ProviderAuthVendor &vendor,
                              std::string primary_token);
  void OnSecondaryExchangeResponse(const std::string &flow_id,
                                   std::string primary_token,
                                   std::optional<std::string> body);
  static std::optional<browser::account::mojom::ProviderOauthRecordPtr>
  ParseTokenResponse(const std::string &body);
  static std::optional<browser::account::mojom::ProviderOauthRecordPtr>
  ParseSecondaryTokenResponse(const std::string &provider_id,
                              const std::string &body,
                              const std::string &primary_token);

  // Shared machinery (profile_provider_auth_broker_network.cc).
  static std::unique_ptr<network::SimpleURLLoader> MakePost(
      const std::string &url, std::string body, const char *content_type);
  // The second exchange's request, composed from the row rather than from
  // this function's own idea of what an authorized GET looks like: the scheme
  // the token is presented under and the headers the endpoint requires are
  // both vendor facts, and both live in `kVendors`.
  static std::unique_ptr<network::SimpleURLLoader> MakeSecondaryFetch(
      const ProviderAuthVendor &vendor, const std::string &bearer);
  void PostForm(PendingFlow &flow, const std::string &url, std::string body,
                base::OnceCallback<void(std::optional<std::string>)> callback);
  void PostJson(PendingFlow &flow, const std::string &url, std::string body,
                base::OnceCallback<void(std::optional<std::string>)> callback);
  void PostSecondary(
      PendingFlow &flow, const ProviderAuthVendor &vendor,
      const std::string &bearer,
      base::OnceCallback<void(std::optional<std::string>)> callback);
  void FinishFlow(const std::string &flow_id,
                  core_service::mojom::AuthCallbackStatus status,
                  std::optional<std::string> code_handle,
                  browser::account::mojom::ProviderFlowEventKind event);
  void SubmitTerminal(const PendingFlow &flow,
                      core_service::mojom::AuthCallbackStatus status,
                      std::optional<std::string> code_handle,
                      base::OnceCallback<void(bool)> submitted);
  void NotifyEvent(const std::string &provider_id, const std::string &flow_id,
                   browser::account::mojom::ProviderFlowEventKind kind,
                   const std::optional<std::string> &verification_url,
                   const std::optional<std::string> &user_code);
  // `spent_refresh_token` is the rotation credential this leg presented. A
  // vendor that rotates answers with a new one and this copy is dropped; a
  // vendor that does not answers without one, and this is the only place the
  // previous value still exists to be carried onto the rotated record.
  void OnRefreshResponse(const std::string &provider_id,
                         std::string spent_refresh_token,
                         std::optional<std::string> body);
  void OnSecondaryRefreshResponse(const std::string &provider_id,
                                  std::string primary_token,
                                  std::optional<std::string> body);
  void OnRevocationDone(network::SimpleURLLoader *loader,
                        std::optional<std::string> body);
  void ResolveRefresh(const std::string &provider_id,
                      browser::account::mojom::ProviderOauthRecordPtr record,
                      bool definitive);

  SEQUENCE_CHECKER(sequence_checker_);
  const raw_ptr<ProfileAccountBroker> account_broker_;
  scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory_;
  FlowTerminalCallback flow_terminal_;
  // Keyed by flow id; at most a handful, bounded by the core's own
  // MAX_PENDING_SIGN_IN_FLOWS admission bound.
  std::map<std::string, PendingFlow> flows_;
  std::map<std::string, RefreshFlight> refreshes_;
  std::vector<std::unique_ptr<network::SimpleURLLoader>> revocations_;
  base::WeakPtrFactory<ProfileProviderAuthBroker> weak_factory_{this};
};

}  // namespace taffy

#endif  // TAFFY_BROWSER_PROVIDERAUTH_PROFILE_PROVIDER_AUTH_BROKER_H_

// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <stddef.h>

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "base/functional/bind.h"
#include "base/json/json_writer.h"
#include "base/values.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "taffy/browser/account/profile_account_broker.h"
#include "taffy/browser/providerauth/profile_provider_auth_broker.h"
#include "taffy/browser/providerauth/provider_auth_wire.h"

// The authorization-code half: what the authorization request says, what the
// exchange spends, and the two vendor deviations decision 0095 carries as
// facts of a row rather than as behaviour of the broker.

namespace taffy {
namespace {

namespace account_mojom = browser::account::mojom;
namespace service = core_service::mojom;

constexpr size_t kVerifierEntropyBytes = 48u;

// A field is appended only where the row has one. A vendor that names no
// client must not be sent an empty `client_id`, and a flow that presented no
// redirect must not name one at the token endpoint either — RFC 6749 asks
// for it in the exchange exactly when the authorization request carried one.
void AppendField(std::string &body, const std::string &key,
                 std::string_view value) {
  if (value.empty()) {
    return;
  }
  body += "&" + provider_auth::FormField(key, std::string(value));
}

}  // namespace

void ProfileProviderAuthBroker::StartPkceFlow(
    PendingFlow &flow, const ProviderAuthVendor &vendor) {
  flow.pkce_verifier = provider_auth::RandomBase64Url(kVerifierEntropyBytes);
  flow.redirect_uri = std::string(ProviderAuthRedirectFor(vendor));

  // One vendor expects the OAuth `state` parameter to be the PKCE verifier
  // rather than an independent nonce, and its server checks that it is. The
  // deviation is carried because parity with the tool whose identity the row
  // borrows is the mitigation decision 0095 names, and refusing it would
  // leave a row that can never work.
  //
  // What it costs is real and is bounded here rather than argued away. Making
  // state the verifier puts the verifier in the redirect, so anything that
  // can read the redirect can also spend a stolen code — which is the one
  // thing PKCE exists to prevent. Decision 0095 section 2 is what makes that
  // acceptable on this product and nowhere else: the redirect is matched and
  // cancelled before it connects, no socket is opened, and no page ever sees
  // it. The manual path shows the person the same two values on the product's
  // own chrome, which is what the borrowed tool does too.
  //
  // Two things keep the deviation inside this one row. The claim comparison
  // is untouched — it still compares one flow-owned base64url value in
  // constant time, and for every other vendor that value is `state` itself,
  // so nothing about the other rows changes shape. And `state` stays
  // independent entropy for every vendor without exception, because `state`
  // is what the terminal echoes across the seam to the isolated core: the
  // verifier is browser-only material and this is where it stops.
  if (vendor.state_is_pkce_verifier) {
    flow.authorization_state = flow.pkce_verifier;
  }

  std::string url = std::string(vendor.authorization_url) +
                    "?response_type=code&" +
                    provider_auth::FormField("state", flow.authorization_state) +
                    "&" +
                    provider_auth::FormField(
                        "code_challenge",
                        provider_auth::PkceChallenge(flow.pkce_verifier)) +
                    "&code_challenge_method=S256";
  AppendField(url, "client_id", vendor.client_id);
  // The address goes in whatever this row calls it. Empty means what RFC 6749
  // calls it, which is every row but the one whose callback is its caller's
  // to choose rather than a registration to match.
  AppendField(url, vendor.authorization_redirect_param[0] == '\0'
                       ? "redirect_uri"
                       : vendor.authorization_redirect_param,
              flow.redirect_uri);
  AppendField(url, "scope", vendor.scope);
  // The vendor's own extra parameters, appended last and appended as pairs.
  // A value is escaped by the same composer every other value goes through,
  // so a table entry cannot introduce a parameter of its own — which is the
  // one thing a row full of query text could do, and the reason the column is
  // typed (see `ProviderAuthAuthorizationParam`).
  for (const ProviderAuthAuthorizationParam &param :
       vendor.extra_authorization_params) {
    AppendField(url, param.name, param.value);
  }
  if (vendor.authorization_displays_code) {
    // The vendor is being asked to show the person the code instead of
    // redirecting anywhere, which is why this row has no address to match.
    url += "&code=true";
  }
  NotifyEvent(flow.provider_id, flow.flow_id,
              account_mojom::ProviderFlowEventKind::kAwaitingAuthorization,
              std::nullopt, std::nullopt);
  account_broker_->OpenProviderAuthSurface(
      flow.flow_id, url,
      base::BindOnce(&ProfileProviderAuthBroker::OnAuthSurfaceOpened,
                     weak_factory_.GetWeakPtr(), flow.flow_id));
}

void ProfileProviderAuthBroker::OnAuthSurfaceOpened(const std::string &flow_id,
                                                    bool opened) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (opened) {
    return;
  }
  auto found = flows_.find(flow_id);
  if (found == flows_.end()) {
    return;
  }
  PendingFlow &flow = found->second;
  const ProviderAuthVendor *vendor = ProviderAuthVendorFor(flow.provider_id);
  if (vendor && vendor->device_code_url[0] != '\0') {
    // No surface opened, and this vendor also runs the shape that needs none
    // (decision 0095 section 3 prefers it anyway). The PKCE state of the flow
    // is dropped whole so nothing can claim it as a redirect afterwards.
    flow.pkce_verifier.clear();
    flow.redirect_uri.clear();
    flow.authorization_state = flow.state;
    StartDeviceFlow(flow, *vendor);
    return;
  }
  FinishFlow(flow_id, service::AuthCallbackStatus::kPlatformUnavailable,
             std::nullopt,
             account_mojom::ProviderFlowEventKind::kFailedUnavailable);
}

void ProfileProviderAuthBroker::OnCodeSealed(
    const std::string &flow_id, std::string code,
    std::optional<std::string> handle) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto found = flows_.find(flow_id);
  if (found == flows_.end()) {
    std::fill(code.begin(), code.end(), '\0');
    return;
  }
  if (!handle) {
    std::fill(code.begin(), code.end(), '\0');
    FinishFlow(flow_id, service::AuthCallbackStatus::kPlatformUnavailable,
               std::nullopt,
               account_mojom::ProviderFlowEventKind::kFailedUnavailable);
    return;
  }
  SubmitTerminal(found->second, service::AuthCallbackStatus::kAuthorizationCode,
                 *handle,
                 base::BindOnce(&ProfileProviderAuthBroker::OnTerminalSubmitted,
                                weak_factory_.GetWeakPtr(), flow_id,
                                std::move(code)));
  // The one-shot handle exists for the terminal command alone; nothing will
  // ever consume it, so it is dropped rather than left among the vault's
  // bounded transient slots.
  account_broker_->DeleteProviderTransient(*handle);
}

void ProfileProviderAuthBroker::OnTerminalSubmitted(const std::string &flow_id,
                                                    std::string code,
                                                    bool accepted) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto found = flows_.find(flow_id);
  if (found == flows_.end()) {
    std::fill(code.begin(), code.end(), '\0');
    return;
  }
  PendingFlow &flow = found->second;
  if (!accepted) {
    std::fill(code.begin(), code.end(), '\0');
    NotifyEvent(flow.provider_id, flow_id,
                account_mojom::ProviderFlowEventKind::kFailedUnavailable,
                std::nullopt, std::nullopt);
    flows_.erase(found);
    return;
  }
  const ProviderAuthVendor *vendor = ProviderAuthVendorFor(flow.provider_id);
  if (flow.pkce_verifier.empty() || !vendor) {
    // Device flow: `code` is the token-response body the poll already
    // exchanged; the pending marker is clear and the tokens can land.
    StoreTokens(flow_id, code);
    std::fill(code.begin(), code.end(), '\0');
    return;
  }
  if (vendor->exchange == ProviderAuthExchangeKind::kMintedApiKey) {
    // This vendor's exchange mints a key rather than returning a grant, and
    // it takes its arguments as a document rather than as a form.
    base::DictValue arguments;
    arguments.Set("code", code);
    arguments.Set("code_verifier", flow.pkce_verifier);
    arguments.Set("code_challenge_method", "S256");
    std::string body;
    const bool encoded = base::JSONWriter::Write(arguments, &body);
    std::fill(code.begin(), code.end(), '\0');
    if (!encoded) {
      FinishFlow(flow_id, service::AuthCallbackStatus::kProviderError,
                 std::nullopt,
                 account_mojom::ProviderFlowEventKind::kFailedProvider);
      return;
    }
    PostJson(flow, vendor->token_url, std::move(body),
             base::BindOnce(&ProfileProviderAuthBroker::OnExchangeResponse,
                            weak_factory_.GetWeakPtr(), flow_id));
    return;
  }
  std::string body =
      provider_auth::FormField("grant_type", "authorization_code") + "&" +
      provider_auth::FormField("code", code) + "&" +
      provider_auth::FormField("code_verifier", flow.pkce_verifier);
  // The exchange names its redirect the way RFC 6749 does or not at all: the
  // renaming above is a property of one vendor's authorization page, and its
  // exchange takes no redirect of any name.
  AppendField(body, "redirect_uri", flow.redirect_uri);
  AppendField(body, "client_id", vendor->client_id);
  // Present only where the vendor's token endpoint requires it. See the
  // column's own comment for why an installed application's is not a secret
  // and why nothing else in this product may grow one.
  AppendField(body, "client_secret", vendor->client_secret);
  std::fill(code.begin(), code.end(), '\0');
  PostForm(flow, vendor->token_url, std::move(body),
           base::BindOnce(&ProfileProviderAuthBroker::OnExchangeResponse,
                          weak_factory_.GetWeakPtr(), flow_id));
}

void ProfileProviderAuthBroker::OnExchangeResponse(
    const std::string &flow_id, std::optional<std::string> body) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto found = flows_.find(flow_id);
  if (found == flows_.end()) {
    return;
  }
  found->second.loader.reset();
  if (!body) {
    NotifyEvent(found->second.provider_id, flow_id,
                account_mojom::ProviderFlowEventKind::kFailedProvider,
                std::nullopt, std::nullopt);
    flows_.erase(found);
    return;
  }
  const ProviderAuthVendor *vendor =
      ProviderAuthVendorFor(found->second.provider_id);
  if (vendor && vendor->exchange == ProviderAuthExchangeKind::kMintedApiKey) {
    StoreMintedKey(flow_id, *body);
  } else {
    StoreTokens(flow_id, *body);
  }
  std::fill(body->begin(), body->end(), '\0');
}

}  // namespace taffy

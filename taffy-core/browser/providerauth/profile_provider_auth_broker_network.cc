// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <stddef.h>

#include <algorithm>
#include <optional>
#include <string>
#include <utility>

#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/time/time.h"
#include "base/values.h"
#include "mojo/public/cpp/bindings/callback_helpers.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "services/network/public/mojom/url_response_head.mojom.h"
#include "taffy/browser/account/profile_account_broker.h"
#include "taffy/browser/providerauth/profile_provider_auth_broker.h"
#include "taffy/browser/providerauth/provider_auth_wire.h"
#include "url/gurl.h"

// The shared network machinery of the provider auth broker: the request
// shapes every leg uses, the terminal and event seams, the refresh
// single-flight and the best-effort revocation. Vendor bodies never enter
// logs; token material is zeroed on every path.

namespace taffy {
namespace {

namespace account_mojom = browser::account::mojom;
namespace service = core_service::mojom;

constexpr size_t kMaxTokenResponseBytes = 64u * 1024u;
constexpr base::TimeDelta kLegTimeout = base::Seconds(30);
constexpr char kFormContentType[] = "application/x-www-form-urlencoded";
constexpr char kJsonContentType[] = "application/json";

constexpr net::NetworkTrafficAnnotationTag kTrafficAnnotation =
    net::DefineNetworkTrafficAnnotation("taffy_provider_auth", R"(
      semantics {
        sender: "TaffyGo provider subscription sign-in"
        description:
          "Signs in to a model vendor's subscription the person already "
          "pays for: the device-authorization and token legs of RFC 8628, "
          "the PKCE code exchange, the second exchange a few vendors "
          "require after the first, the token refresh, and best-effort "
          "revocation on sign-out. Every endpoint is a compiled constant."
        trigger:
          "The person presses Connect on the AI-and-providers screen, a "
          "stored token needs refreshing, or they disconnect a plan."
        data:
          "OAuth protocol fields only: client id, scopes, PKCE values, "
          "device or authorization codes, and refresh tokens. Never page "
          "content, keys for other vendors, or account-plane tokens."
        destination: OTHER
      }
      policy {
        cookies_allowed: NO
        setting:
          "Disconnect the plan on the AI-and-providers screen; no other "
          "setting reaches these endpoints."
        policy_exception_justification:
          "The request only happens on an explicit sign-in gesture."
      })");

std::unique_ptr<network::ResourceRequest> MakeRequest(const std::string &url,
                                                     const char *method) {
  auto request = std::make_unique<network::ResourceRequest>();
  request->url = GURL(url);
  request->method = method;
  request->credentials_mode = network::mojom::CredentialsMode::kOmit;
  request->redirect_mode = network::mojom::RedirectMode::kError;
  request->headers.SetHeader("Accept", "application/json");
  return request;
}

std::unique_ptr<network::SimpleURLLoader> MakeLoader(
    std::unique_ptr<network::ResourceRequest> request) {
  std::unique_ptr<network::SimpleURLLoader> loader =
      network::SimpleURLLoader::Create(std::move(request), kTrafficAnnotation);
  loader->SetAllowHttpErrorResults(true);
  loader->SetRetryOptions(0, network::SimpleURLLoader::RETRY_NEVER);
  loader->SetTimeoutDuration(kLegTimeout);
  return loader;
}

}  // namespace

std::unique_ptr<network::SimpleURLLoader>
ProfileProviderAuthBroker::MakePost(const std::string &url, std::string body,
                                    const char *content_type) {
  std::unique_ptr<network::SimpleURLLoader> loader =
      MakeLoader(MakeRequest(url, "POST"));
  loader->AttachStringForUpload(std::move(body), content_type);
  return loader;
}

// The one request shape that carries a credential in a header rather than in
// a body, and the one that carries no body at all: the second exchange
// presents the first exchange's token and takes back the credential the
// product will actually use.
//
// Two things about it are the vendor's rather than OAuth's, so both are read
// off the row. The scheme, because this endpoint is an internal one of that
// vendor's and takes the token under the legacy `token` keyword; and a short
// list of headers naming the client, because it refuses a request that names
// none. Neither can carry a credential — the scheme is a keyword and the
// headers are compiled constants — so nothing sensitive reaches this from the
// table.
std::unique_ptr<network::SimpleURLLoader>
ProfileProviderAuthBroker::MakeSecondaryFetch(const ProviderAuthVendor &vendor,
                                              const std::string &bearer) {
  std::unique_ptr<network::ResourceRequest> request =
      MakeRequest(vendor.secondary_token_url, "GET");
  const std::string scheme = vendor.secondary_authorization_scheme[0] == '\0'
                                 ? "Bearer"
                                 : vendor.secondary_authorization_scheme;
  request->headers.SetHeader("Authorization", scheme + " " + bearer);
  for (const ProviderAuthAuthorizationParam &header :
       vendor.secondary_request_headers) {
    request->headers.SetHeader(header.name, header.value);
  }
  return MakeLoader(std::move(request));
}

void ProfileProviderAuthBroker::PostForm(
    PendingFlow &flow, const std::string &url, std::string body,
    base::OnceCallback<void(std::optional<std::string>)> callback) {
  flow.loader = MakePost(url, std::move(body), kFormContentType);
  flow.loader->DownloadToString(
      url_loader_factory_.get(),
      mojo::WrapCallbackWithDefaultInvokeIfNotRun(std::move(callback),
                                                  std::nullopt),
      kMaxTokenResponseBytes);
}

void ProfileProviderAuthBroker::PostJson(
    PendingFlow &flow, const std::string &url, std::string body,
    base::OnceCallback<void(std::optional<std::string>)> callback) {
  flow.loader = MakePost(url, std::move(body), kJsonContentType);
  flow.loader->DownloadToString(
      url_loader_factory_.get(),
      mojo::WrapCallbackWithDefaultInvokeIfNotRun(std::move(callback),
                                                  std::nullopt),
      kMaxTokenResponseBytes);
}

void ProfileProviderAuthBroker::PostSecondary(
    PendingFlow &flow, const ProviderAuthVendor &vendor,
    const std::string &bearer,
    base::OnceCallback<void(std::optional<std::string>)> callback) {
  flow.loader = MakeSecondaryFetch(vendor, bearer);
  flow.loader->DownloadToString(
      url_loader_factory_.get(),
      mojo::WrapCallbackWithDefaultInvokeIfNotRun(std::move(callback),
                                                  std::nullopt),
      kMaxTokenResponseBytes);
}

void ProfileProviderAuthBroker::FinishFlow(
    const std::string &flow_id, service::AuthCallbackStatus status,
    std::optional<std::string> code_handle,
    account_mojom::ProviderFlowEventKind event) {
  auto found = flows_.find(flow_id);
  if (found == flows_.end()) {
    return;
  }
  SubmitTerminal(found->second, status, std::move(code_handle),
                 base::BindOnce([](bool) {}));
  NotifyEvent(found->second.provider_id, flow_id, event, std::nullopt,
              std::nullopt);
  flows_.erase(found);
}

void ProfileProviderAuthBroker::SubmitTerminal(
    const PendingFlow &flow, service::AuthCallbackStatus status,
    std::optional<std::string> code_handle,
    base::OnceCallback<void(bool)> submitted) {
  if (!flow_terminal_) {
    std::move(submitted).Run(false);
    return;
  }
  auto command = service::ProviderAuthCallbackCommand::New();
  command->flow_id = flow.flow_id;
  command->redirect_binding_id = flow.redirect_binding_id;
  // The flow's own nonce, never the value sent as the OAuth `state`. For
  // every vendor but one those are the same string; for that one the sent
  // value is the PKCE verifier, which is browser-only material and must not
  // be echoed into the isolated core.
  command->returned_state = flow.state;
  command->status = status;
  command->authorization_code_handle = std::move(code_handle);
  flow_terminal_.Run(flow.service_generation, std::move(command),
                     std::move(submitted));
}

void ProfileProviderAuthBroker::NotifyEvent(
    const std::string &provider_id, const std::string &flow_id,
    account_mojom::ProviderFlowEventKind kind,
    const std::optional<std::string> &verification_url,
    const std::optional<std::string> &user_code) {
  if (!account_broker_) {
    return;
  }
  auto event = account_mojom::ProviderFlowEvent::New();
  event->provider_id = provider_id;
  event->flow_id = flow_id;
  event->kind = kind;
  event->verification_url = verification_url;
  event->user_code = user_code;
  account_broker_->NotifyProviderFlowEvent(std::move(event));
}

void ProfileProviderAuthBroker::RefreshCredential(
    const std::string &provider_id, std::string refresh_token,
    RefreshCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!callback) {
    std::fill(refresh_token.begin(), refresh_token.end(), '\0');
    return;
  }
  const ProviderAuthVendor *vendor = ProviderAuthVendorFor(provider_id);
  // A minted key does not expire and nothing rotates it, so there is no leg
  // to run — the refusal is the row's shape rather than a branch, and a
  // vendor whose exchange mints a key can never acquire one by acquiring an
  // endpoint. The identity is checked rather than the terms review: a
  // credential already in force is refreshed on its own merits, and a review
  // date withdrawn after a sign-in must stop the next sign-in, not sign the
  // person out of the one they have.
  if (!vendor || vendor->client_id[0] == '\0' ||
      vendor->exchange == ProviderAuthExchangeKind::kMintedApiKey ||
      refresh_token.empty() || !url_loader_factory_) {
    std::fill(refresh_token.begin(), refresh_token.end(), '\0');
    std::move(callback).Run(nullptr, false);
    return;
  }
  RefreshFlight &flight = refreshes_[provider_id];
  flight.callbacks.push_back(std::move(callback));
  if (flight.loader) {
    // Single-flight: the caller joins the refresh already running and the
    // rotation credential it carried is not spent a second time.
    std::fill(refresh_token.begin(), refresh_token.end(), '\0');
    return;
  }
  if (vendor->secondary_token_url[0] != '\0') {
    // This vendor has no refresh grant. What its record calls a refresh token
    // is the first exchange's token, and renewing means buying a fresh
    // credential with it exactly as the sign-in did.
    flight.loader = MakeSecondaryFetch(*vendor, refresh_token);
    flight.loader->DownloadToString(
        url_loader_factory_.get(),
        mojo::WrapCallbackWithDefaultInvokeIfNotRun(
            base::BindOnce(
                &ProfileProviderAuthBroker::OnSecondaryRefreshResponse,
                weak_factory_.GetWeakPtr(), provider_id, refresh_token),
            std::nullopt),
        kMaxTokenResponseBytes);
    std::fill(refresh_token.begin(), refresh_token.end(), '\0');
    return;
  }
  std::string body =
      provider_auth::FormField("grant_type", "refresh_token") + "&" +
      provider_auth::FormField("client_id", vendor->client_id) + "&" +
      provider_auth::FormField("refresh_token", refresh_token);
  // The same field the exchange sends, for the same vendor and the same
  // reason: its token endpoint requires it on every grant, refresh included.
  if (vendor->client_secret[0] != '\0') {
    body += "&" + provider_auth::FormField("client_secret",
                                           vendor->client_secret);
  }
  flight.loader = MakePost(vendor->token_url, std::move(body),
                           kFormContentType);
  // The spent credential is carried into the answer, and the local copy is
  // zeroed only after the bind has taken its own. See OnRefreshResponse: a
  // vendor that does not rotate leaves this the only surviving copy, and
  // dropping it would seal a record with no way to renew itself.
  flight.loader->DownloadToString(
      url_loader_factory_.get(),
      mojo::WrapCallbackWithDefaultInvokeIfNotRun(
          base::BindOnce(&ProfileProviderAuthBroker::OnRefreshResponse,
                         weak_factory_.GetWeakPtr(), provider_id,
                         refresh_token),
          std::nullopt),
      kMaxTokenResponseBytes);
  std::fill(refresh_token.begin(), refresh_token.end(), '\0');
}

void ProfileProviderAuthBroker::OnRefreshResponse(
    const std::string &provider_id, std::string spent_refresh_token,
    std::optional<std::string> body) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!body) {
    std::fill(spent_refresh_token.begin(), spent_refresh_token.end(), '\0');
    ResolveRefresh(provider_id, nullptr, false);
    return;
  }
  std::optional<account_mojom::ProviderOauthRecordPtr> record =
      ParseTokenResponse(*body);
  if (record) {
    // Not every vendor rotates. RFC 6749 section 6 makes the new refresh
    // token optional, and a vendor that omits it means "keep using the one
    // you have" rather than "you have none" — so the record that replaces the
    // sealed one carries the spent credential forward. Without this the next
    // expiry finds an access token that is stale and no way to renew it, and
    // the person is signed out roughly one token lifetime after signing in,
    // over and over, with nothing anywhere reporting a failure.
    if (!(*record)->refresh_token && !spent_refresh_token.empty()) {
      (*record)->refresh_token.emplace(spent_refresh_token.begin(),
                                       spent_refresh_token.end());
    }
    std::fill(spent_refresh_token.begin(), spent_refresh_token.end(), '\0');
    std::fill(body->begin(), body->end(), '\0');
    ResolveRefresh(provider_id, std::move(*record), false);
    return;
  }
  std::fill(spent_refresh_token.begin(), spent_refresh_token.end(), '\0');
  // The one distinction that matters (decision 0078): a dead grant needs the
  // person again; anything else is the network's fault and the record stays.
  std::optional<base::Value> parsed =
      base::JSONReader::Read(*body, base::JSON_PARSE_RFC);
  const base::DictValue *dict = parsed ? parsed->GetIfDict() : nullptr;
  const std::string *error = dict ? dict->FindString("error") : nullptr;
  const bool definitive = error && *error == "invalid_grant";
  std::fill(body->begin(), body->end(), '\0');
  ResolveRefresh(provider_id, nullptr, definitive);
}

void ProfileProviderAuthBroker::OnSecondaryRefreshResponse(
    const std::string &provider_id, std::string primary_token,
    std::optional<std::string> body) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  std::optional<account_mojom::ProviderOauthRecordPtr> record =
      body ? ParseSecondaryTokenResponse(provider_id, *body, primary_token)
           : std::nullopt;
  std::fill(primary_token.begin(), primary_token.end(), '\0');
  if (body) {
    std::fill(body->begin(), body->end(), '\0');
  }
  if (record) {
    ResolveRefresh(provider_id, std::move(*record), false);
    return;
  }
  // Nothing here can tell a dead grant from a bad afternoon: this exchange
  // answers with a document rather than an OAuth error, so the honest answer
  // is transient and the person is asked again only when the credential
  // itself is refused in use.
  ResolveRefresh(provider_id, nullptr, false);
}

void ProfileProviderAuthBroker::ResolveRefresh(
    const std::string &provider_id,
    account_mojom::ProviderOauthRecordPtr record, bool definitive) {
  auto found = refreshes_.find(provider_id);
  if (found == refreshes_.end()) {
    return;
  }
  std::vector<RefreshCallback> callbacks = std::move(found->second.callbacks);
  refreshes_.erase(found);
  for (size_t index = 0; index < callbacks.size(); ++index) {
    // Every joined caller gets an answer; only the first gets the record,
    // because token buffers must have exactly one owner to zero them.
    std::move(callbacks[index])
        .Run(index == 0 ? std::move(record) : nullptr, definitive);
  }
}

void ProfileProviderAuthBroker::RevokeBestEffort(const std::string &provider_id,
                                                 std::string token) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  const ProviderAuthVendor *vendor = ProviderAuthVendorFor(provider_id);
  if (!vendor || vendor->revocation_url[0] == '\0' || token.empty() ||
      !url_loader_factory_) {
    std::fill(token.begin(), token.end(), '\0');
    return;
  }
  std::string body = provider_auth::FormField("token", token);
  std::fill(token.begin(), token.end(), '\0');
  std::unique_ptr<network::SimpleURLLoader> loader =
      MakePost(vendor->revocation_url, std::move(body), kFormContentType);
  network::SimpleURLLoader *raw_loader = loader.get();
  revocations_.push_back(std::move(loader));
  raw_loader->DownloadToString(
      url_loader_factory_.get(),
      base::BindOnce(&ProfileProviderAuthBroker::OnRevocationDone,
                     weak_factory_.GetWeakPtr(), raw_loader),
      kMaxTokenResponseBytes);
}

void ProfileProviderAuthBroker::OnRevocationDone(
    network::SimpleURLLoader *loader, std::optional<std::string> body) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  std::erase_if(revocations_, [loader](const auto &held) {
    return held.get() == loader;
  });
}

}  // namespace taffy

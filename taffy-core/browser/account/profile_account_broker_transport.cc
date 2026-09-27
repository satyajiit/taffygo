// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/account/profile_account_broker.h"

#include <algorithm>
#include <ranges>
#include <string_view>
#include <utility>

#include "base/functional/bind.h"
#include "base/time/time.h"
#include "net/base/load_flags.h"
#include "net/base/net_errors.h"
#include "net/http/http_request_headers.h"
#include "net/http/http_response_headers.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "mojo/public/cpp/bindings/callback_helpers.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "services/network/public/mojom/url_loader.mojom.h"
#include "services/network/public/mojom/url_response_head.mojom.h"
#include "taffy/browser/account_plane_configuration.h"

namespace taffy {
namespace {

namespace service = core_service::mojom;

void ClearString(std::string* value) {
  if (!value) {
    return;
  }
  std::ranges::fill(*value, '\0');
  value->clear();
}

uint64_t NowMonotonicMillisForTransport() {
  const int64_t value = base::TimeTicks::Now().since_origin().InMilliseconds();
  return value < 0 ? 0u : static_cast<uint64_t>(value);
}

}  // namespace

std::optional<GURL> ProfileAccountBroker::AccountEndpoint(
    AccountEndpointKind kind) const {
  if (ValidateAccountPlaneConfiguration(TAFFY_ACCOUNT_API_ORIGIN,
                                        TAFFY_ACCOUNT_PUBLISHABLE_KEY) !=
      AccountPlaneConfigurationStatus::kReady) {
    return std::nullopt;
  }
  std::string_view path;
  switch (kind) {
    case AccountEndpointKind::kPkceToken:
      path = "/auth/v1/token?grant_type=pkce";
      break;
    case AccountEndpointKind::kNativeToken:
      path = "/auth/v1/token?grant_type=id_token";
      break;
    case AccountEndpointKind::kRefreshToken:
      path = "/auth/v1/token?grant_type=refresh_token";
      break;
    case AccountEndpointKind::kLogout:
      path = "/auth/v1/logout?scope=global";
      break;
    case AccountEndpointKind::kEmailOtp:
      path = "/auth/v1/otp";
      break;
  }
  const GURL origin(TAFFY_ACCOUNT_API_ORIGIN);
  const GURL endpoint = origin.Resolve(path);
  if (!endpoint.is_valid() || endpoint.DeprecatedGetOriginAsURL() !=
                                  origin.DeprecatedGetOriginAsURL()) {
    return std::nullopt;
  }
  return endpoint;
}

bool ProfileAccountBroker::StartJsonRequest(
    const service::EffectEnvelope& effect,
    const GURL& endpoint,
    std::string request_body,
    std::optional<std::string> bearer_token,
    NetworkResponseCallback callback) {
  const uint64_t now = NowMonotonicMillisForTransport();
  if (!effect.operation || effect.operation->deadline_monotonic_ms <= now) {
    ClearString(&request_body);
    if (bearer_token) {
      ClearString(&*bearer_token);
    }
    NetworkResponse response;
    response.net_error = net::ERR_TIMED_OUT;
    std::move(callback).Run(std::move(response));
    return false;
  }
  if (effect.effect_id.empty() || network_loaders_.contains(effect.effect_id)) {
    ClearString(&request_body);
    if (bearer_token) {
      ClearString(&*bearer_token);
    }
    NetworkResponse response;
    response.net_error = net::ERR_INVALID_ARGUMENT;
    std::move(callback).Run(std::move(response));
    return false;
  }

  auto request = std::make_unique<network::ResourceRequest>();
  request->url = endpoint;
  request->method = "POST";
  request->load_flags = net::LOAD_DISABLE_CACHE | net::LOAD_BYPASS_CACHE;
  request->credentials_mode = network::mojom::CredentialsMode::kOmit;
  request->redirect_mode = network::mojom::RedirectMode::kError;
  request->headers.SetHeader("apikey", TAFFY_ACCOUNT_PUBLISHABLE_KEY);
  request->headers.SetHeader(net::HttpRequestHeaders::kAccept,
                             "application/json");
  if (bearer_token) {
    request->headers.SetHeader(net::HttpRequestHeaders::kAuthorization,
                               "Bearer " + *bearer_token);
    ClearString(&*bearer_token);
  }
  constexpr net::NetworkTrafficAnnotationTag kTrafficAnnotation =
      net::DefineNetworkTrafficAnnotation("taffy_account_plane", R"(
        semantics {
          sender: "TaffyGo account broker"
          description: "Exchanges or revokes a person-requested account session."
          trigger: "The person starts, refreshes, or ends the profile account session."
          data: "Bounded account credentials; no browsing content or URLs."
          destination: OTHER
          internal { contacts { owners: "//taffy/OWNERS" } }
          user_data { type: ACCESS_TOKEN }
          last_reviewed: "2026-08-22"
        }
        policy {
          cookies_allowed: NO
          setting: "The person controls the account session from settings."
          policy_exception_justification: "Not implemented."
        })");
  auto loader =
      network::SimpleURLLoader::Create(std::move(request), kTrafficAnnotation);
  loader->SetAllowHttpErrorResults(true);
  loader->SetRetryOptions(0, network::SimpleURLLoader::RETRY_NEVER);
  loader->SetTimeoutDuration(
      base::Milliseconds(effect.operation->deadline_monotonic_ms - now));
  loader->AttachStringForUpload(std::move(request_body), "application/json");
  network::SimpleURLLoader* raw_loader = loader.get();
  network_loaders_.emplace(effect.effect_id, std::move(loader));
  raw_loader->DownloadToString(
      url_loader_factory_.get(),
      mojo::WrapCallbackWithDefaultInvokeIfNotRun(
          base::BindOnce(&ProfileAccountBroker::OnNetworkComplete,
                         weak_factory_.GetWeakPtr(), effect.effect_id,
                         std::move(callback)),
          std::nullopt),
      effect.network_request->max_response_bytes);
  return true;
}

void ProfileAccountBroker::OnNetworkComplete(
    std::string effect_id,
    NetworkResponseCallback callback,
    std::optional<std::string> response_body) {
  NetworkResponse response;
  response.dispatched = true;
  auto found = network_loaders_.find(effect_id);
  if (found == network_loaders_.end()) {
    if (response_body) {
      ClearString(&*response_body);
    }
    std::move(callback).Run(std::move(response));
    return;
  }
  std::unique_ptr<network::SimpleURLLoader> loader = std::move(found->second);
  network_loaders_.erase(found);
  response.net_error = loader->NetError();
  if (loader->ResponseInfo() && loader->ResponseInfo()->headers) {
    response.http_status = loader->ResponseInfo()->headers->response_code();
  }
  response.body = std::move(response_body);
  loader.reset();
  std::move(callback).Run(std::move(response));
}

}  // namespace taffy

// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/model/profile_provider_listing_fetcher.h"

#include <algorithm>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/containers/span.h"
#include "base/functional/bind.h"
#include "base/strings/strcat.h"
#include "base/time/time.h"
#include "crypto/secure_util.h"
#include "mojo/public/cpp/bindings/callback_helpers.h"
#include "net/base/load_flags.h"
#include "net/base/net_errors.h"
#include "net/http/http_request_headers.h"
#include "net/http/http_response_headers.h"
#include "net/http/http_status_code.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "services/network/public/mojom/url_loader.mojom.h"
#include "services/network/public/mojom/url_response_head.mojom.h"
#include "taffy/browser/core_model_effect_validation.h"
#include "taffy/browser/providerauth/provider_credential_host.h"

namespace taffy {
namespace {

namespace service = core_service::mojom;

// One remote listing attempt. Credential resolution may consume part of an
// operation's explicit allowance; the transport gets the smaller remainder.
// Listing effects planned today carry no external deadline, so this remains
// the exact whole allowance in the ordinary path.
constexpr base::TimeDelta kListingTimeout = base::Seconds(15);

void ClearString(std::string* value) {
  if (!value) {
    return;
  }
  crypto::SecureZeroBuffer(base::as_writable_byte_span(*value));
  value->clear();
}

uint64_t NowMonotonicMillis() {
  const int64_t value = base::TimeTicks::Now().since_origin().InMilliseconds();
  return value < 0 ? 0u : static_cast<uint64_t>(value);
}

bool IsUsableCredential(const std::string& credential,
                        std::string_view prefix) {
  if (credential.empty() ||
      prefix.size() > service::kMaxModelHeaderValueBytes ||
      credential.size() > service::kMaxModelHeaderValueBytes - prefix.size()) {
    return false;
  }
  // Provider credentials are opaque tokens. Whitespace and control bytes are
  // neither necessary nor safe in the one header this module owns.
  for (char character : credential) {
    const auto byte = static_cast<unsigned char>(character);
    if (byte < 0x21u || byte > 0x7Eu) {
      return false;
    }
  }
  return true;
}

bool IsTemporaryStatus(int status) {
  return status == net::HTTP_REQUEST_TIMEOUT ||
         status == net::HTTP_TOO_MANY_REQUESTS ||
         (status >= 500 && status < 600);
}

service::EffectResultPtr MakeResult(
    const service::EffectEnvelope& effect,
    service::CatalogFetchDisposition disposition,
    std::vector<uint8_t> body) {
  auto result = service::EffectResult::New();
  result->operation = effect.operation.Clone();
  result->effect_id = effect.effect_id;
  // The disposition is the verdict carrier. Completed says the browser leg
  // reached a terminal answer, not that the provider returned a usable list.
  result->status = service::EffectStatus::kCompleted;
  result->kind = service::EffectKind::kFetchProviderListing;
  result->provider_listing = service::ProviderListingFetchResult::New(
      effect.provider_listing_fetch->provider_id, disposition, std::move(body));
  return result;
}

constexpr net::NetworkTrafficAnnotationTag kListingTrafficAnnotation =
    net::DefineNetworkTrafficAnnotation("taffy_provider_model_listing", R"(
      semantics {
        sender: "TaffyGo provider model listing"
        description:
          "Fetches the model list offered to the person by a connected model "
          "provider. The response stays unread in the browser and is decoded "
          "only by the sandboxed core's catalog decoder."
        trigger:
          "The person connected a provider whose catalog row says that the "
          "provider serves its model list, and the isolated core says the "
          "list is due for refresh."
        data:
          "The provider credential in one authorization header. No page "
          "content, prompt, model response, cookie, account identifier, or "
          "device identifier."
        destination: OTHER
        internal { contacts { owners: "//taffy/OWNERS" } }
        user_data { type: ACCESS_TOKEN }
        last_reviewed: "2026-08-31"
      }
      policy {
        cookies_allowed: NO
        setting:
          "The person connects the provider in settings and can remove its "
          "credential there. With no credential, no listing is fetched."
        policy_exception_justification: "No enterprise policy exists yet."
      })");

}  // namespace

ProfileProviderListingFetcher::ProfileProviderListingFetcher(
    scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory)
    : url_loader_factory_(std::move(url_loader_factory)) {}

ProfileProviderListingFetcher::~ProfileProviderListingFetcher() = default;

// static
std::optional<ProfileProviderListingFetcher::ListingRoute>
ProfileProviderListingFetcher::RouteFor(std::string_view provider_id,
                                        service::ProviderWireApi wire_api) {
  // Provider identity and family are one closed key. The provider row names
  // only its canonical origin; both paths beneath it remain compiled here.
  // Each path below is the vendor's own prefix from `kProviderPathPrefixes` in
  // profile_model_broker_routes.cc, joined to the same `/v1` the family
  // composes for a model call, and then the authenticated list decision 0098
  // asks for. The two tables are written side by side on purpose: a vendor
  // whose model calls go one place and whose list is fetched from another is
  // a vendor nobody meant to describe.
  //
  // The row a vendor does *not* get is one whose list this build cannot read.
  // A route here is what makes the core plan a fetch, so compiling one for a
  // provider whose body no dialect understands buys a request, a refusal and
  // a retry on every refresh — see `dialect.rs` in the core for what is read
  // and `LISTED_MODEL_ROWS` in the model-router's baseline test for the
  // catalog half of the same pairing.
  constexpr ListingRoute kRoutes[] = {
      {"chutes", service::ProviderWireApi::kOpenAiCompletions, "/v1/models",
       net::HttpRequestHeaders::kAuthorization, "Bearer ",
       /*accepts_credential_origin=*/false},
      {"kilocode", service::ProviderWireApi::kOpenAiCompletions,
       "/api/gateway/v1/models", net::HttpRequestHeaders::kAuthorization,
       "Bearer ", /*accepts_credential_origin=*/false},
      {"novita", service::ProviderWireApi::kOpenAiCompletions,
       "/openai/v1/models", net::HttpRequestHeaders::kAuthorization, "Bearer ",
       /*accepts_credential_origin=*/false},
      {"openrouter", service::ProviderWireApi::kOpenAiCompletions,
       "/api/v1/models", net::HttpRequestHeaders::kAuthorization, "Bearer ",
       /*accepts_credential_origin=*/false},
      {"venice", service::ProviderWireApi::kOpenAiCompletions, "/api/v1/models",
       net::HttpRequestHeaders::kAuthorization, "Bearer ",
       /*accepts_credential_origin=*/false},
  };
  for (const ListingRoute& route : kRoutes) {
    if (route.provider_id == provider_id && route.wire_api == wire_api) {
      return route;
    }
  }
  return std::nullopt;
}

// static
std::optional<GURL> ProfileProviderListingFetcher::ComposeUrl(
    const std::string& endpoint,
    const ListingRoute& route) {
  if (!IsCanonicalHttpsProviderOrigin(endpoint)) {
    return std::nullopt;
  }
  const GURL origin(endpoint);
  const GURL url = origin.Resolve(route.path);
  if (!url.is_valid() || !url.SchemeIs("https") || url.has_ref() ||
      url.DeprecatedGetOriginAsURL() != origin.DeprecatedGetOriginAsURL()) {
    return std::nullopt;
  }
  return url;
}

void ProfileProviderListingFetcher::Perform(
    service::EffectEnvelopePtr effect,
    const CredentialResolver& credential_resolver,
    EffectCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!effect || !effect->operation ||
      effect->kind != service::EffectKind::kFetchProviderListing ||
      !effect->provider_listing_fetch) {
    std::move(callback).Run(nullptr);
    return;
  }
  const service::ProviderListingFetchEffect& fetch =
      *effect->provider_listing_fetch;
  if (pending_) {
    std::move(callback).Run(MakeResult(
        *effect, service::CatalogFetchDisposition::kUnavailable, {}));
    return;
  }

  const std::optional<ListingRoute> route =
      RouteFor(fetch.provider_id, fetch.wire_api);
  const std::optional<GURL> url =
      route ? ComposeUrl(fetch.endpoint, *route) : std::nullopt;
  const bool valid_handle =
      fetch.credential_handle && !fetch.credential_handle->empty() &&
      fetch.credential_handle->size() <= service::kMaxIdentifierBytes;
  if (!route || !url || !valid_handle || fetch.max_response_bytes == 0u ||
      fetch.max_response_bytes > service::kMaxProviderListingBytes ||
      !url_loader_factory_ || !credential_resolver) {
    std::move(callback).Run(MakeResult(
        *effect, service::CatalogFetchDisposition::kUnavailable, {}));
    return;
  }

  const uint64_t generation = effect->operation->service_generation;
  const std::string effect_id = effect->effect_id;
  pending_ = std::make_unique<PendingFetch>(
      PendingFetch{std::move(effect), std::move(callback), *url, *route});
  credential_resolver.Run(
      fetch.provider_id, *fetch.credential_handle,
      mojo::WrapCallbackWithDefaultInvokeIfNotRun(
          base::BindOnce(&ProfileProviderListingFetcher::OnCredentialResolved,
                         weak_factory_.GetWeakPtr(), generation, effect_id),
          std::nullopt, std::nullopt));
}

void ProfileProviderListingFetcher::CancelGeneration(uint64_t generation) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!pending_ || !pending_->effect || !pending_->effect->operation ||
      pending_->effect->operation->service_generation != generation) {
    return;
  }
  loader_.reset();
  pending_.reset();
}

bool ProfileProviderListingFetcher::has_fetch() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return pending_ != nullptr;
}

void ProfileProviderListingFetcher::OnCredentialResolved(
    uint64_t generation,
    std::string effect_id,
    std::optional<std::string> credential,
    std::optional<std::string> credential_origin) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  const auto clear_credential = [&credential]() {
    if (credential) {
      ClearString(&*credential);
    }
  };
  if (!pending_ || !pending_->effect || !pending_->effect->operation ||
      pending_->effect->operation->service_generation != generation ||
      pending_->effect->effect_id != effect_id) {
    clear_credential();
    return;
  }
  if (!credential ||
      !IsUsableCredential(*credential, pending_->route.credential_prefix)) {
    clear_credential();
    Finish(service::CatalogFetchDisposition::kUnavailable, {});
    return;
  }

  if (credential_origin) {
    if (!pending_->route.accepts_credential_origin ||
        !ProviderCredentialHostLicensed(pending_->route.provider_id,
                                        *credential_origin) ||
        !ProviderCredentialHostAddresses(
            pending_->effect->provider_listing_fetch->endpoint,
            *credential_origin)) {
      clear_credential();
      Finish(service::CatalogFetchDisposition::kUnavailable, {});
      return;
    }
    const std::optional<GURL> substituted =
        ComposeUrl(*credential_origin, pending_->route);
    if (!substituted) {
      clear_credential();
      Finish(service::CatalogFetchDisposition::kUnavailable, {});
      return;
    }
    pending_->url = *substituted;
  }

  base::TimeDelta timeout = kListingTimeout;
  const uint64_t deadline = pending_->effect->operation->deadline_monotonic_ms;
  if (deadline != 0u) {
    const uint64_t now = NowMonotonicMillis();
    if (deadline <= now) {
      clear_credential();
      Finish(service::CatalogFetchDisposition::kUnavailable, {});
      return;
    }
    timeout = std::min(timeout, base::Milliseconds(deadline - now));
  }

  auto request = std::make_unique<network::ResourceRequest>();
  request->url = pending_->url;
  request->method = "GET";
  request->credentials_mode = network::mojom::CredentialsMode::kOmit;
  request->redirect_mode = network::mojom::RedirectMode::kError;
  request->load_flags = net::LOAD_DISABLE_CACHE | net::LOAD_BYPASS_CACHE |
                        net::LOAD_DO_NOT_SAVE_COOKIES;
  request->headers.SetHeader(net::HttpRequestHeaders::kAccept,
                             "application/json");
  std::string credential_header_value =
      base::StrCat({pending_->route.credential_prefix, *credential});
  request->headers.SetHeader(std::string(pending_->route.credential_header),
                             credential_header_value);
  ClearString(&credential_header_value);
  clear_credential();

  loader_ = network::SimpleURLLoader::Create(std::move(request),
                                             kListingTrafficAnnotation);
  loader_->SetAllowHttpErrorResults(true);
  loader_->SetRetryOptions(0, network::SimpleURLLoader::RETRY_NEVER);
  loader_->SetTimeoutDuration(timeout);
  network::SimpleURLLoader* raw_loader = loader_.get();
  raw_loader->DownloadToString(
      url_loader_factory_.get(),
      mojo::WrapCallbackWithDefaultInvokeIfNotRun(
          base::BindOnce(&ProfileProviderListingFetcher::OnFetched,
                         weak_factory_.GetWeakPtr(), generation, effect_id),
          std::nullopt),
      pending_->effect->provider_listing_fetch->max_response_bytes);
}

void ProfileProviderListingFetcher::OnFetched(uint64_t generation,
                                              std::string effect_id,
                                              std::optional<std::string> body) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!pending_ || !loader_ || !pending_->effect ||
      !pending_->effect->operation ||
      pending_->effect->operation->service_generation != generation ||
      pending_->effect->effect_id != effect_id) {
    return;
  }

  const int net_error = loader_->NetError();
  int http_status = 0;
  std::string content_type;
  const network::mojom::URLResponseHead* head = loader_->ResponseInfo();
  if (head && head->headers) {
    http_status = head->headers->response_code();
    head->headers->GetMimeType(&content_type);
  }
  loader_.reset();

  if (net_error == net::ERR_INSUFFICIENT_RESOURCES) {
    Finish(service::CatalogFetchDisposition::kOversized, {});
    return;
  }
  if (net_error != net::OK || !body || http_status == 0 ||
      IsTemporaryStatus(http_status)) {
    Finish(service::CatalogFetchDisposition::kUnavailable, {});
    return;
  }
  if (http_status != net::HTTP_OK || content_type != "application/json" ||
      body->empty()) {
    Finish(service::CatalogFetchDisposition::kMalformedTransport, {});
    return;
  }
  std::vector<uint8_t> bytes(body->begin(), body->end());
  Finish(service::CatalogFetchDisposition::kSuccess, std::move(bytes));
}

void ProfileProviderListingFetcher::Finish(
    service::CatalogFetchDisposition disposition,
    std::vector<uint8_t> body) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!pending_) {
    return;
  }
  loader_.reset();
  std::unique_ptr<PendingFetch> pending = std::move(pending_);
  service::EffectResultPtr result =
      MakeResult(*pending->effect, disposition, std::move(body));
  std::move(pending->callback).Run(std::move(result));
}

}  // namespace taffy

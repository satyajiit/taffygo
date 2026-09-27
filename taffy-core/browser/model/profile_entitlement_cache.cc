// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/model/profile_entitlement_cache.h"

#include <algorithm>
#include <utility>

#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/time/time.h"
#include "base/values.h"
#include "mojo/public/cpp/bindings/callback_helpers.h"
#include "net/base/load_flags.h"
#include "net/base/net_errors.h"
#include "net/http/http_request_headers.h"
#include "net/http/http_response_headers.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "services/network/public/mojom/url_loader.mojom.h"
#include "services/network/public/mojom/url_response_head.mojom.h"
#include "taffy/browser/managed_route_configuration.h"
#include "url/gurl.h"

namespace taffy {
namespace {

namespace service = core_service::mojom;

// The worker's mint route, under the compiled origin and nowhere else.
constexpr std::string_view kMintPath = "/v1/entitlement/mint";

// The mint response is counts, identifiers and one token; this is generous.
constexpr size_t kMaxMintResponseBytes = 64 * 1024;

// A held token this close to its expiry is not handed out: the dispatch it
// would ride can outlive it between here and the worker's verify.
constexpr uint64_t kTokenFreshnessFloorSeconds = 60;

void ClearString(std::string* value) {
  if (!value) {
    return;
  }
  std::ranges::fill(*value, '\0');
  value->clear();
}

uint64_t NowUtcMillis() {
  const int64_t value = base::Time::Now().InMillisecondsSinceUnixEpoch();
  return value < 0 ? 0u : static_cast<uint64_t>(value);
}

uint64_t NowUtcSeconds() { return NowUtcMillis() / 1000u; }

// A non-negative count from a JSON value that may have parsed as int or
// double. Doubles are the reality for anything past 2^31 − 1 — an epoch
// second will be one within this product's lifetime — and refusing them
// would make the parse fail on a calendar date.
std::optional<uint64_t> ReadCount(const base::DictValue& dict,
                                  std::string_view key) {
  const base::Value* value = dict.Find(key);
  if (!value) {
    return std::nullopt;
  }
  if (value->is_int()) {
    const int number = value->GetInt();
    return number < 0 ? std::nullopt
                      : std::optional<uint64_t>(static_cast<uint64_t>(number));
  }
  if (value->is_double()) {
    const double number = value->GetDouble();
    if (number < 0 || number > 9007199254740992.0) {
      return std::nullopt;
    }
    return static_cast<uint64_t>(number);
  }
  return std::nullopt;
}

// An ISO 8601 instant as epoch seconds; zero for null, absent, or a spelling
// the platform's parser does not read. Zero is the contract's own "unstated"
// and a surface treats it as such, so an unreadable date degrades to an
// unstated one rather than failing the summary that carried it.
uint64_t EpochSecondsFrom(const base::DictValue& dict,
                          std::string_view key) {
  const std::string* text = dict.FindString(key);
  if (!text || text->empty()) {
    return 0u;
  }
  base::Time parsed;
  if (!base::Time::FromUTCString(text->c_str(), &parsed)) {
    return 0u;
  }
  const int64_t seconds = (parsed - base::Time::UnixEpoch()).InSeconds();
  return seconds < 0 ? 0u : static_cast<uint64_t>(seconds);
}

}  // namespace

ProfileEntitlementCache::MintWaiter::MintWaiter() = default;
ProfileEntitlementCache::MintWaiter::MintWaiter(MintWaiter&&) = default;
ProfileEntitlementCache::MintWaiter& ProfileEntitlementCache::MintWaiter::
operator=(MintWaiter&&) = default;
ProfileEntitlementCache::MintWaiter::~MintWaiter() = default;

ProfileEntitlementCache::ProfileEntitlementCache(
    scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory,
    std::string worker_origin, AccessTokenProvider access_token_provider)
    : url_loader_factory_(std::move(url_loader_factory)),
      worker_origin_(std::move(worker_origin)),
      access_token_provider_(std::move(access_token_provider)) {}

ProfileEntitlementCache::~ProfileEntitlementCache() {
  ClearString(&unspent_token_);
}

bool ProfileEntitlementCache::has_unspent_token_for_testing() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return !unspent_token_.empty();
}

void ProfileEntitlementCache::FetchSummary(SummaryCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  MintWaiter waiter;
  waiter.summary_callback = std::move(callback);
  waiters_.push_back(std::move(waiter));
  if (!loader_) {
    StartMint();
  }
}

void ProfileEntitlementCache::AcquireToken(bool evict, TokenCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (evict) {
    // The 402 path: whatever is held bought the refusal, and the summary it
    // arrived beside is the same mint's answer. Both go.
    DropToken();
    last_summary_ = nullptr;
  }
  if (!unspent_token_.empty() &&
      token_expires_at_seconds_ >
          NowUtcSeconds() + kTokenFreshnessFloorSeconds) {
    // Spent by the handing-out: the worker replays the token's identity, so
    // a copy kept here would only ever buy a refusal.
    std::string token = std::move(unspent_token_);
    unspent_token_.clear();
    token_expires_at_seconds_ = 0;
    std::move(callback).Run(std::move(token));
    return;
  }
  DropToken();
  MintWaiter waiter;
  waiter.token_callback = std::move(callback);
  waiters_.push_back(std::move(waiter));
  if (!loader_) {
    StartMint();
  }
}

void ProfileEntitlementCache::Clear() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DropToken();
  last_summary_ = nullptr;
  // The mint still in flight, if any, was the departed account's; its answer
  // must not be installed for whoever signs in next, and its waiters get the
  // honest "nothing" now rather than that answer later.
  loader_.reset();
  FailWaiters();
}

void ProfileEntitlementCache::CancelPendingMint() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  loader_.reset();
  waiters_.clear();
}

void ProfileEntitlementCache::DropToken() {
  ClearString(&unspent_token_);
  token_expires_at_seconds_ = 0;
}

void ProfileEntitlementCache::StartMint() {
  if (ValidateManagedRouteConfiguration(worker_origin_) !=
          ManagedRouteConfigurationStatus::kReady ||
      !url_loader_factory_ || !access_token_provider_) {
    FailWaiters();
    return;
  }
  access_token_provider_.Run(mojo::WrapCallbackWithDefaultInvokeIfNotRun(
      base::BindOnce(&ProfileEntitlementCache::OnAccessToken,
                     weak_factory_.GetWeakPtr()),
      std::nullopt));
}

void ProfileEntitlementCache::OnAccessToken(
    std::optional<std::string> access_token) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (waiters_.empty()) {
    // Cleared while the account plane was being asked.
    if (access_token) {
      ClearString(&*access_token);
    }
    return;
  }
  if (!access_token || access_token->empty()) {
    // No usable session is a transport failure, never a definitive absence:
    // the worker was not asked, so nothing definitive was learned.
    FailWaiters();
    return;
  }

  const GURL origin(worker_origin_);
  const GURL url = origin.Resolve(kMintPath);
  if (!url.is_valid() ||
      url.DeprecatedGetOriginAsURL() != origin.DeprecatedGetOriginAsURL()) {
    ClearString(&*access_token);
    FailWaiters();
    return;
  }

  auto resource_request = std::make_unique<network::ResourceRequest>();
  resource_request->url = url;
  resource_request->method = "POST";
  resource_request->load_flags =
      net::LOAD_DISABLE_CACHE | net::LOAD_BYPASS_CACHE;
  // The GoTrue bearer is the request's whole identity; an ambient cookie
  // would be a second one.
  resource_request->credentials_mode = network::mojom::CredentialsMode::kOmit;
  // A redirect would send the account's bearer wherever the response
  // pointed.
  resource_request->redirect_mode = network::mojom::RedirectMode::kError;
  resource_request->headers.SetHeader(
      net::HttpRequestHeaders::kAuthorization,
      std::string("Bearer ") + *access_token);
  resource_request->headers.SetHeader(net::HttpRequestHeaders::kAccept,
                                      "application/json");
  ClearString(&*access_token);

  constexpr net::NetworkTrafficAnnotationTag kTrafficAnnotation =
      net::DefineNetworkTrafficAnnotation("taffy_entitlement_mint", R"(
        semantics {
          sender: "TaffyGo entitlement mint"
          description: "Asks the product's own service whether the signed-in "
                       "account may use the included assistant plan, and for "
                       "the short-lived token one such request rides on."
          trigger: "The person is signed in and uses, or is about to use, "
                   "the included assistant plan."
          data: "The signed-in session's bearer token. No page content, no "
                "prompt text, and no body at all."
          destination: GOOGLE_OWNED_SERVICE
          internal { contacts { owners: "//taffy/OWNERS" } }
          user_data {
            type: ACCESS_TOKEN
          }
          last_reviewed: "2026-08-28"
        }
        policy {
          cookies_allowed: NO
          setting: "Signing out stops these requests entirely."
          policy_exception_justification: "Not implemented."
        })");

  auto loader = network::SimpleURLLoader::Create(std::move(resource_request),
                                                 kTrafficAnnotation);
  // The refusal body is the verdict — a 403 carrying `no_entitlement` is the
  // one definitive absence — so error bodies are read, not dropped.
  loader->SetAllowHttpErrorResults(true);
  // A retry would be a second mint under a second rail slot; the protocol in
  // the core owns retrying by planning another fetch.
  loader->SetRetryOptions(0, network::SimpleURLLoader::RETRY_NEVER);
  loader->SetTimeoutDuration(base::Seconds(30));
  loader->AttachStringForUpload("{}", "application/json");

  network::SimpleURLLoader* raw_loader = loader.get();
  loader_ = std::move(loader);
  raw_loader->DownloadToString(
      url_loader_factory_.get(),
      mojo::WrapCallbackWithDefaultInvokeIfNotRun(
          base::BindOnce(&ProfileEntitlementCache::OnMintResponse,
                         weak_factory_.GetWeakPtr()),
          std::nullopt),
      kMaxMintResponseBytes);
}

void ProfileEntitlementCache::OnMintResponse(
    std::optional<std::string> response_body) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!loader_) {
    return;
  }
  const int net_error = loader_->NetError();
  int http_status = 0;
  const network::mojom::URLResponseHead* head = loader_->ResponseInfo();
  if (head && head->headers) {
    http_status = head->headers->response_code();
  }
  loader_.reset();

  if (net_error != net::OK || !response_body) {
    FailWaiters();
    return;
  }
  if (http_status >= 200 && http_status < 300) {
    if (!InstallMintedResponse(*response_body)) {
      FailWaiters();
      return;
    }
    AnswerWaiters(last_summary_.Clone());
    return;
  }
  if (http_status == 403) {
    // The worker's definitive answer over the account's own signed session:
    // no entitlement exists. This is the one non-2xx that installs anything.
    DropToken();
    auto summary = service::EntitlementSummaryResult::New();
    summary->definitive_absent = true;
    summary->minted_at_utc_ms = NowUtcMillis();
    last_summary_ = summary.Clone();
    AnswerWaiters(std::move(summary));
    return;
  }
  // 401 (a stale session), 429 (a rail), 5xx and everything else: the worker
  // was not definitively heard on the entitlement question. The last state
  // stands and a later poke or dispatch asks again.
  FailWaiters();
}

bool ProfileEntitlementCache::InstallMintedResponse(const std::string& body) {
  std::optional<base::DictValue> parsed =
      base::JSONReader::ReadDict(body, base::JSON_PARSE_RFC, 16);
  if (!parsed) {
    return false;
  }
  const std::optional<uint64_t> schema_version =
      ReadCount(*parsed, "schema_version");
  if (schema_version != 1u) {
    return false;
  }
  const std::string* token = parsed->FindString("token");
  const std::optional<uint64_t> token_expires_at =
      ReadCount(*parsed, "token_expires_at");
  const std::string* plan_id = parsed->FindString("plan_id");
  const base::DictValue* window = parsed->FindDict("window");
  const base::DictValue* requests = parsed->FindDict("requests");
  const base::DictValue* credits = parsed->FindDict("credits");
  const base::ListValue* models = parsed->FindList("models");
  if (!token || token->empty() || !token_expires_at || !plan_id ||
      plan_id->empty() || plan_id->size() > service::kMaxIdentifierBytes ||
      !window || !requests || !credits || !models ||
      models->size() > service::kMaxEntitledModels) {
    return false;
  }
  const std::optional<uint64_t> window_seconds = ReadCount(*window, "seconds");
  const std::optional<uint64_t> requests_used = ReadCount(*requests, "used");
  const std::optional<uint64_t> requests_limit = ReadCount(*requests, "limit");
  const std::optional<uint64_t> credits_granted =
      ReadCount(*credits, "granted");
  const std::optional<uint64_t> credits_remaining =
      ReadCount(*credits, "remaining");
  const std::optional<uint64_t> credit_unit =
      ReadCount(*credits, "unit_micro_usd");
  if (!window_seconds || *window_seconds == 0u ||
      *window_seconds > 0xFFFFFFFFu || !requests_used || !requests_limit ||
      !credits_granted || !credits_remaining || !credit_unit ||
      *credit_unit == 0u) {
    return false;
  }
  auto summary = service::EntitlementSummaryResult::New();
  summary->definitive_absent = false;
  summary->plan_id = *plan_id;
  summary->model_ids.reserve(models->size());
  for (const base::Value& model : *models) {
    const std::string* model_id = model.GetIfString();
    if (!model_id || model_id->empty() ||
        model_id->size() > service::kMaxIdentifierBytes) {
      return false;
    }
    summary->model_ids.push_back(*model_id);
  }
  summary->window_seconds = static_cast<uint32_t>(*window_seconds);
  summary->requests_remaining = *requests_limit > *requests_used
                                    ? *requests_limit - *requests_used
                                    : 0u;
  summary->credits_granted = *credits_granted;
  summary->credits_remaining = *credits_remaining;
  summary->credit_unit_micros = *credit_unit;
  summary->next_renewal_epoch_seconds =
      EpochSecondsFrom(*credits, "next_turn_at");
  summary->valid_until_epoch_seconds = EpochSecondsFrom(*parsed, "valid_until");
  summary->minted_at_utc_ms = NowUtcMillis();
  const GURL origin(worker_origin_);
  // One deployment serves both roles today; the two fields exist so a split
  // is a data change, not a schema change.
  summary->worker_host = origin.host();
  summary->gateway_host = origin.host();

  DropToken();
  unspent_token_ = *token;
  token_expires_at_seconds_ = *token_expires_at;
  last_summary_ = std::move(summary);
  return true;
}

void ProfileEntitlementCache::AnswerWaiters(
    service::EntitlementSummaryResultPtr summary) {
  const bool token_minted = summary && !summary->definitive_absent;
  std::vector<MintWaiter> waiters = std::move(waiters_);
  waiters_.clear();
  for (MintWaiter& waiter : waiters) {
    if (waiter.summary_callback) {
      std::move(waiter.summary_callback).Run(summary.Clone());
      continue;
    }
    if (!waiter.token_callback) {
      continue;
    }
    if (!unspent_token_.empty()) {
      std::string token = std::move(unspent_token_);
      unspent_token_.clear();
      token_expires_at_seconds_ = 0;
      std::move(waiter.token_callback).Run(std::move(token));
    } else if (token_minted) {
      // A single-use token serves one dispatch, and this mint's has been
      // spent on an earlier waiter. The next dispatch needs its own mint —
      // requeued rather than refused, because the worker is answering.
      waiters_.push_back(std::move(waiter));
    } else {
      // The mint answered without a token — a definitive absence. There is
      // nothing to ride a dispatch on, and minting again buys the same
      // answer.
      std::move(waiter.token_callback).Run(std::nullopt);
    }
  }
  if (!waiters_.empty() && !loader_) {
    StartMint();
  }
}

void ProfileEntitlementCache::FailWaiters() {
  std::vector<MintWaiter> waiters = std::move(waiters_);
  waiters_.clear();
  for (MintWaiter& waiter : waiters) {
    if (waiter.summary_callback) {
      std::move(waiter.summary_callback).Run(nullptr);
    } else if (waiter.token_callback) {
      std::move(waiter.token_callback).Run(std::nullopt);
    }
  }
}

}  // namespace taffy

// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Where a listing fetch goes, and what it is allowed to carry there.
//
// The seam from the suite next door is the moment the request leaves: this
// file is about the address and the credential, and
// profile_provider_listing_fetcher_unittest.cc is about what comes back and
// what may be in flight. They drive the same object and are separate files
// because a vendor added to the route table changes only one of them, and a
// five-vendor table walked in a suite that also owns deadlines and
// generations is a suite nobody reads.
//
// The fixture is a copy under its own name rather than a shared header, and
// the name is not an aesthetic choice: gtest keys a suite by its name string
// and requires every test in one to use the same fixture class, so two files
// declaring `ProfileProviderListingFetcherTest` over two anonymous-namespace
// classes fails at registration with no assertion to read.

#include "taffy/browser/model/profile_provider_listing_fetcher.h"

#include <stdint.h>

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "base/functional/bind.h"
#include "base/memory/scoped_refptr.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/test/bind.h"
#include "base/test/task_environment.h"
#include "net/base/load_flags.h"
#include "net/base/net_errors.h"
#include "net/http/http_response_headers.h"
#include "net/http/http_status_code.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "services/network/public/cpp/url_loader_completion_status.h"
#include "services/network/public/cpp/weak_wrapper_shared_url_loader_factory.h"
#include "services/network/public/mojom/url_loader.mojom.h"
#include "services/network/public/mojom/url_response_head.mojom.h"
#include "services/network/test/test_url_loader_factory.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace taffy {
namespace {

namespace service = core_service::mojom;

constexpr char kOrigin[] = "https://openrouter.ai";
constexpr char kListingUrl[] = "https://openrouter.ai/api/v1/models";
constexpr char kCredential[] = "sk-or-fixture";
constexpr uint32_t kResponseLimit = 1024u;

service::EffectEnvelopePtr ListingEffect(uint64_t generation = 7u,
                                         std::string effect_id = "listing-1") {
  auto effect = service::EffectEnvelope::New();
  effect->operation = service::OperationEnvelope::New(
      "provider-listing-openrouter-1", generation, 0u, 0u,
      "provider-listing-key-openrouter-1");
  effect->effect_id = std::move(effect_id);
  effect->kind = service::EffectKind::kFetchProviderListing;
  effect->retry_class = service::RetryClass::kIdempotent;
  effect->provider_listing_fetch = service::ProviderListingFetchEffect::New(
      "openrouter", kOrigin, service::ProviderWireApi::kOpenAiCompletions,
      std::optional<std::string>("credential-handle-1"), kResponseLimit);
  return effect;
}

class ProfileProviderListingRouteTest : public testing::Test {
 protected:
  void SetUp() override {
    shared_factory_ =
        base::MakeRefCounted<network::WeakWrapperSharedURLLoaderFactory>(
            &factory_);
    fetcher_ = std::make_unique<ProfileProviderListingFetcher>(shared_factory_);
    factory_.SetInterceptor(base::BindLambdaForTesting(
        [this](const network::ResourceRequest& request) {
          observed_ = request;
        }));
  }

  // The address is a parameter here and nowhere else in this pair of suites,
  // because this is the file that drives more than one vendor. The media type
  // is not optional to get right: the fetcher classifies an answer by it, so a
  // body registered without one comes back as a malformed transport and reads
  // as a route that reached the wrong place.
  void RespondAt(const GURL& url,
                 net::HttpStatusCode status,
                 std::string_view body,
                 std::string_view content_type = "application/json") {
    auto head = network::mojom::URLResponseHead::New();
    head->headers = base::MakeRefCounted<net::HttpResponseHeaders>(base::StrCat(
        {"HTTP/1.1 ", base::NumberToString(static_cast<int>(status))}));
    head->headers->SetHeader("Content-Type", std::string(content_type));
    head->mime_type = std::string(content_type);
    factory_.AddResponse(url, std::move(head), std::string(body),
                         network::URLLoaderCompletionStatus(net::OK));
  }

  void Respond(net::HttpStatusCode status,
               std::string_view body,
               std::string_view content_type = "application/json") {
    RespondAt(GURL(kListingUrl), status, body, content_type);
  }

  ProfileProviderListingFetcher::CredentialResolver ImmediateCredential(
      std::optional<std::string> material = std::string(kCredential),
      std::optional<std::string> origin = std::nullopt) {
    return base::BindRepeating(
        [](std::optional<std::string> material,
           std::optional<std::string> origin, const std::string&,
           const std::string&,
           ProfileProviderListingFetcher::ProviderCredentialCallback reply) {
          std::move(reply).Run(material, origin);
        },
        std::move(material), std::move(origin));
  }

  service::EffectResultPtr Fetch(
      service::EffectEnvelopePtr effect = ListingEffect()) {
    service::EffectResultPtr result;
    bool answered = false;
    fetcher_->Perform(
        std::move(effect), ImmediateCredential(),
        base::BindLambdaForTesting([&](service::EffectResultPtr answer) {
          answered = true;
          result = std::move(answer);
        }));
    environment_.RunUntilIdle();
    EXPECT_TRUE(answered);
    return result;
  }

  base::test::TaskEnvironment environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME,
      base::test::TaskEnvironment::MainThreadType::IO};
  network::TestURLLoaderFactory factory_;
  scoped_refptr<network::SharedURLLoaderFactory> shared_factory_;
  std::unique_ptr<ProfileProviderListingFetcher> fetcher_;
  std::optional<network::ResourceRequest> observed_;
};
TEST_F(ProfileProviderListingRouteTest,
       OpenRouterUsesTheCompiledAuthenticatedRouteAndNoAmbientState) {
  Respond(net::HTTP_OK, R"({"data":[{"id":"vendor/model"}]})");
  service::EffectResultPtr result = Fetch();

  ASSERT_TRUE(result);
  ASSERT_TRUE(result->provider_listing);
  EXPECT_EQ(result->status, service::EffectStatus::kCompleted);
  EXPECT_EQ(result->provider_listing->disposition,
            service::CatalogFetchDisposition::kSuccess);
  EXPECT_EQ(std::string(result->provider_listing->body.begin(),
                        result->provider_listing->body.end()),
            R"({"data":[{"id":"vendor/model"}]})");
  ASSERT_TRUE(observed_);
  EXPECT_EQ(observed_->url, GURL(kListingUrl));
  EXPECT_EQ(observed_->method, "GET");
  EXPECT_EQ(observed_->credentials_mode,
            network::mojom::CredentialsMode::kOmit);
  EXPECT_EQ(observed_->redirect_mode, network::mojom::RedirectMode::kError);
  EXPECT_EQ(observed_->headers.GetHeader("Accept"),
            std::optional<std::string>("application/json"));
  EXPECT_EQ(observed_->headers.GetHeader("Authorization"),
            std::optional<std::string>("Bearer sk-or-fixture"));
  EXPECT_EQ(observed_->url.spec().find(kCredential), std::string::npos);
  EXPECT_NE(observed_->load_flags & net::LOAD_DISABLE_CACHE, 0);
  EXPECT_NE(observed_->load_flags & net::LOAD_BYPASS_CACHE, 0);
  EXPECT_NE(observed_->load_flags & net::LOAD_DO_NOT_SAVE_COOKIES, 0);
}

// Every vendor that serves its own list, at the address it serves it from.
//
// Each of these is one string in two places that cannot see each other: this
// table, and `LISTED_MODEL_ROWS` in the model-router's `embedded_baseline`
// test, which pins the origin the catalog row names. Getting the pair wrong
// produces a fetch that reaches a real host at a path that is not a listing,
// and the core answers that with "not a listing I can read" — a refusal that
// reads as a vendor changing its API rather than as a typo here.
//
// `chutes` is the row worth having: it is the one vendor with no prefix at
// all, so a table that lost its prefix column entirely would still pass on
// that row and fail on the other four.
TEST_F(ProfileProviderListingRouteTest, EachListedVendorHasItsOwnAddress) {
  const struct {
    const char* provider_id;
    const char* origin;
    const char* expected;
  } kCases[] = {
      {"openrouter", "https://openrouter.ai",
       "https://openrouter.ai/api/v1/models"},
      {"venice", "https://api.venice.ai", "https://api.venice.ai/api/v1/models"},
      {"novita", "https://api.novita.ai",
       "https://api.novita.ai/openai/v1/models"},
      {"kilocode", "https://api.kilo.ai",
       "https://api.kilo.ai/api/gateway/v1/models"},
      {"chutes", "https://llm.chutes.ai", "https://llm.chutes.ai/v1/models"},
  };
  for (const auto& test_case : kCases) {
    observed_.reset();
    factory_.ClearResponses();
    RespondAt(GURL(test_case.expected), net::HTTP_OK, R"({"data":[]})");
    auto effect = ListingEffect();
    effect->provider_listing_fetch->provider_id = test_case.provider_id;
    effect->provider_listing_fetch->endpoint = test_case.origin;
    service::EffectResultPtr result = Fetch(std::move(effect));
    ASSERT_TRUE(result) << test_case.provider_id;
    ASSERT_TRUE(result->provider_listing) << test_case.provider_id;
    EXPECT_EQ(result->provider_listing->disposition,
              service::CatalogFetchDisposition::kSuccess)
        << test_case.provider_id;
    ASSERT_TRUE(observed_) << test_case.provider_id;
    EXPECT_EQ(observed_->url, GURL(test_case.expected));
    // The credential travels in the header and never in the address, for
    // every vendor rather than for the one this suite grew up around.
    EXPECT_EQ(observed_->headers.GetHeader("Authorization"),
              std::optional<std::string>("Bearer sk-or-fixture"))
        << test_case.provider_id;
    EXPECT_EQ(observed_->url.spec().find(kCredential), std::string::npos)
        << test_case.provider_id;
  }
}

TEST_F(ProfileProviderListingRouteTest,
       ProviderAndFamilyAreOneClosedRouteKey) {
  const struct {
    const char* provider_id;
    service::ProviderWireApi wire_api;
  } cases[] = {
      {"openai", service::ProviderWireApi::kOpenAiCompletions},
      {"openrouter", service::ProviderWireApi::kOpenAiResponses},
      {"openrouter", service::ProviderWireApi::kManaged},
  };
  for (const auto& test_case : cases) {
    auto effect = ListingEffect();
    effect->provider_listing_fetch->provider_id = test_case.provider_id;
    effect->provider_listing_fetch->wire_api = test_case.wire_api;
    service::EffectResultPtr result = Fetch(std::move(effect));
    ASSERT_TRUE(result);
    ASSERT_TRUE(result->provider_listing);
    EXPECT_EQ(result->provider_listing->disposition,
              service::CatalogFetchDisposition::kUnavailable);
  }
  for (const uint32_t response_cap :
       {0u, static_cast<uint32_t>(service::kMaxProviderListingBytes + 1u)}) {
    auto effect = ListingEffect();
    effect->provider_listing_fetch->max_response_bytes = response_cap;
    EXPECT_EQ(Fetch(std::move(effect))->provider_listing->disposition,
              service::CatalogFetchDisposition::kUnavailable);
  }
  auto missing_handle = ListingEffect();
  missing_handle->provider_listing_fetch->credential_handle = std::nullopt;
  EXPECT_EQ(Fetch(std::move(missing_handle))->provider_listing->disposition,
            service::CatalogFetchDisposition::kUnavailable);
  EXPECT_FALSE(observed_);
  EXPECT_EQ(factory_.NumPending(), 0);
}

TEST_F(ProfileProviderListingRouteTest,
       OnlyACanonicalHttpsOriginCanReceiveTheCredential) {
  for (const char* endpoint : {
           "http://openrouter.ai",
           "https://openrouter.ai/",
           "https://openrouter.ai/api/v1",
           "https://OPENROUTER.ai",
           "https://openrouter.ai:443",
           "https://user:pass@openrouter.ai",
           "https://openrouter.ai?next=elsewhere",
           "https://openrouter.ai#fragment",
       }) {
    auto effect = ListingEffect();
    effect->provider_listing_fetch->endpoint = endpoint;
    service::EffectResultPtr result = Fetch(std::move(effect));
    ASSERT_TRUE(result);
    ASSERT_TRUE(result->provider_listing);
    EXPECT_EQ(result->provider_listing->disposition,
              service::CatalogFetchDisposition::kUnavailable)
        << endpoint;
  }
  EXPECT_FALSE(observed_);
}

TEST_F(ProfileProviderListingRouteTest,
       AnAbsentOrUnusableCredentialMakesNoRequest) {
  const auto run = [this](std::optional<std::string> material) {
    service::EffectResultPtr result;
    fetcher_->Perform(
        ListingEffect(), ImmediateCredential(std::move(material)),
        base::BindLambdaForTesting([&](service::EffectResultPtr answer) {
          result = std::move(answer);
        }));
    environment_.RunUntilIdle();
    ASSERT_TRUE(result);
    ASSERT_TRUE(result->provider_listing);
    EXPECT_EQ(result->provider_listing->disposition,
              service::CatalogFetchDisposition::kUnavailable);
  };
  run(std::nullopt);
  run(std::string());
  run(std::string("token with spaces"));
  run(std::string("token\r\nX-Injected: yes"));
  EXPECT_FALSE(observed_);
}

TEST_F(ProfileProviderListingRouteTest,
       ACredentialSuppliedOriginIsRefusedByTheLaunchRoute) {
  service::EffectResultPtr result;
  fetcher_->Perform(
      ListingEffect(),
      ImmediateCredential(std::string(kCredential),
                          std::string("https://api.openrouter.ai")),
      base::BindLambdaForTesting([&](service::EffectResultPtr answer) {
        result = std::move(answer);
      }));
  environment_.RunUntilIdle();
  ASSERT_TRUE(result);
  ASSERT_TRUE(result->provider_listing);
  EXPECT_EQ(result->provider_listing->disposition,
            service::CatalogFetchDisposition::kUnavailable);
  EXPECT_FALSE(observed_);
}

}  // namespace
}  // namespace taffy

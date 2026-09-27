// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// What comes back from a listing fetch, and what may be in flight.
//
// The address and the credential are next door in
// profile_provider_listing_route_unittest.cc; everything here starts after the
// request has been composed.

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
#include "base/time/time.h"
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

uint64_t NowMs() {
  const int64_t value = base::TimeTicks::Now().since_origin().InMilliseconds();
  return value < 0 ? 0u : static_cast<uint64_t>(value);
}

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

class ProfileProviderListingFetcherTest : public testing::Test {
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

  void Respond(net::HttpStatusCode status,
               std::string_view body,
               std::string_view content_type = "application/json") {
    auto head = network::mojom::URLResponseHead::New();
    head->headers = base::MakeRefCounted<net::HttpResponseHeaders>(base::StrCat(
        {"HTTP/1.1 ", base::NumberToString(static_cast<int>(status))}));
    head->headers->SetHeader("Content-Type", std::string(content_type));
    head->mime_type = std::string(content_type);
    factory_.AddResponse(GURL(kListingUrl), std::move(head), std::string(body),
                         network::URLLoaderCompletionStatus(net::OK));
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
TEST_F(ProfileProviderListingFetcherTest,
       ResponseClassificationsNeverCarryPartialOrErrorBodies) {
  const struct {
    net::HttpStatusCode status;
    const char* body;
    const char* content_type;
    service::CatalogFetchDisposition expected;
  } cases[] = {
      {net::HTTP_TOO_MANY_REQUESTS, "retry", "application/json",
       service::CatalogFetchDisposition::kUnavailable},
      {net::HTTP_SERVICE_UNAVAILABLE, "later", "application/json",
       service::CatalogFetchDisposition::kUnavailable},
      {net::HTTP_UNAUTHORIZED, "denied", "application/json",
       service::CatalogFetchDisposition::kMalformedTransport},
      {net::HTTP_NOT_FOUND, "wrong route", "application/json",
       service::CatalogFetchDisposition::kMalformedTransport},
      {net::HTTP_PARTIAL_CONTENT, "partial", "application/json",
       service::CatalogFetchDisposition::kMalformedTransport},
      {net::HTTP_OK, "<html></html>", "text/html",
       service::CatalogFetchDisposition::kMalformedTransport},
      {net::HTTP_OK, "", "application/json",
       service::CatalogFetchDisposition::kMalformedTransport},
  };
  for (const auto& test_case : cases) {
    factory_.ClearResponses();
    Respond(test_case.status, test_case.body, test_case.content_type);
    service::EffectResultPtr result = Fetch();
    ASSERT_TRUE(result);
    ASSERT_TRUE(result->provider_listing);
    EXPECT_EQ(result->provider_listing->disposition, test_case.expected)
        << test_case.status;
    EXPECT_TRUE(result->provider_listing->body.empty());
  }
}

TEST_F(ProfileProviderListingFetcherTest,
       AResponsePastTheEffectsExactCapIsOversized) {
  Respond(net::HTTP_OK, std::string(kResponseLimit + 1u, 'x'));
  service::EffectResultPtr result = Fetch();
  ASSERT_TRUE(result);
  ASSERT_TRUE(result->provider_listing);
  EXPECT_EQ(result->provider_listing->disposition,
            service::CatalogFetchDisposition::kOversized);
  EXPECT_TRUE(result->provider_listing->body.empty());
}

TEST_F(ProfileProviderListingFetcherTest,
       ExpiredExternalDeadlineStopsBeforeCredentialLeaves) {
  auto effect = ListingEffect();
  effect->operation->deadline_monotonic_ms = NowMs();
  service::EffectResultPtr result = Fetch(std::move(effect));
  ASSERT_TRUE(result);
  ASSERT_TRUE(result->provider_listing);
  EXPECT_EQ(result->provider_listing->disposition,
            service::CatalogFetchDisposition::kUnavailable);
  EXPECT_FALSE(observed_);
}

TEST_F(ProfileProviderListingFetcherTest,
       TransportUsesTheFixedOrSmallerExternalDeadlineExactly) {
  for (const base::TimeDelta allowance :
       {base::Seconds(15), base::Seconds(4)}) {
    auto effect = ListingEffect(7u, "deadline-test");
    if (allowance < base::Seconds(15)) {
      effect->operation->deadline_monotonic_ms =
          NowMs() + static_cast<uint64_t>(allowance.InMilliseconds());
    }
    service::EffectResultPtr result;
    fetcher_->Perform(
        std::move(effect), ImmediateCredential(),
        base::BindLambdaForTesting([&](service::EffectResultPtr answer) {
          result = std::move(answer);
        }));
    environment_.FastForwardBy(allowance - base::Milliseconds(1));
    EXPECT_FALSE(result);
    environment_.FastForwardBy(base::Milliseconds(1));
    ASSERT_TRUE(result);
    ASSERT_TRUE(result->provider_listing);
    EXPECT_EQ(result->provider_listing->disposition,
              service::CatalogFetchDisposition::kUnavailable);
  }
}

TEST_F(ProfileProviderListingFetcherTest,
       OneFetchAtATimeAndTheSecondIsAnswered) {
  fetcher_->Perform(ListingEffect(), ImmediateCredential(),
                    base::BindOnce([](service::EffectResultPtr) {}));
  ASSERT_TRUE(fetcher_->has_fetch());
  service::EffectResultPtr second;
  fetcher_->Perform(
      ListingEffect(7u, "listing-2"), ImmediateCredential(),
      base::BindLambdaForTesting([&](service::EffectResultPtr answer) {
        second = std::move(answer);
      }));
  ASSERT_TRUE(second);
  ASSERT_TRUE(second->provider_listing);
  EXPECT_EQ(second->provider_listing->disposition,
            service::CatalogFetchDisposition::kUnavailable);
  EXPECT_TRUE(fetcher_->has_fetch());
}

TEST_F(ProfileProviderListingFetcherTest,
       ACancelledGenerationCannotAuthorizeItsSuccessor) {
  std::optional<ProfileProviderListingFetcher::ProviderCredentialCallback>
      old_reply;
  std::optional<ProfileProviderListingFetcher::ProviderCredentialCallback>
      new_reply;
  auto resolver = base::BindRepeating(
      [](std::optional<
             ProfileProviderListingFetcher::ProviderCredentialCallback>*
             old_reply,
         std::optional<
             ProfileProviderListingFetcher::ProviderCredentialCallback>*
             new_reply,
         const std::string&, const std::string&,
         ProfileProviderListingFetcher::ProviderCredentialCallback reply) {
        if (!*old_reply) {
          old_reply->emplace(std::move(reply));
        } else {
          new_reply->emplace(std::move(reply));
        }
      },
      base::Unretained(&old_reply), base::Unretained(&new_reply));
  bool old_answered = false;
  fetcher_->Perform(ListingEffect(7u, "same-id"), resolver,
                    base::BindLambdaForTesting([&](service::EffectResultPtr) {
                      old_answered = true;
                    }));
  ASSERT_TRUE(old_reply);
  fetcher_->CancelGeneration(7u);
  EXPECT_FALSE(fetcher_->has_fetch());

  bool new_answered = false;
  service::EffectResultPtr new_result;
  Respond(net::HTTP_OK, R"({"data":[]})");
  fetcher_->Perform(
      ListingEffect(8u, "same-id"), resolver,
      base::BindLambdaForTesting([&](service::EffectResultPtr answer) {
        new_answered = true;
        new_result = std::move(answer);
      }));
  ASSERT_TRUE(new_reply);
  std::move(*old_reply).Run(std::string("old-secret"), std::nullopt);
  EXPECT_FALSE(observed_);
  EXPECT_FALSE(new_answered);
  std::move(*new_reply).Run(std::string(kCredential), std::nullopt);
  environment_.RunUntilIdle();

  EXPECT_FALSE(old_answered);
  EXPECT_TRUE(new_answered);
  ASSERT_TRUE(new_result);
  ASSERT_TRUE(new_result->provider_listing);
  EXPECT_EQ(new_result->provider_listing->disposition,
            service::CatalogFetchDisposition::kSuccess);
}

TEST_F(ProfileProviderListingFetcherTest,
       AnEnvelopeForAnotherEffectKindAnswersNull) {
  auto effect = ListingEffect();
  effect->kind = service::EffectKind::kModelRequest;
  bool answered = false;
  service::EffectResultPtr result;
  fetcher_->Perform(
      std::move(effect), ImmediateCredential(),
      base::BindLambdaForTesting([&](service::EffectResultPtr answer) {
        answered = true;
        result = std::move(answer);
      }));
  EXPECT_TRUE(answered);
  EXPECT_FALSE(result);
  EXPECT_FALSE(observed_);
}

}  // namespace
}  // namespace taffy

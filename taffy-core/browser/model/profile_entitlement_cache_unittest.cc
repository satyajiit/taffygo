// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/model/profile_entitlement_cache.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/scoped_refptr.h"
#include "base/test/bind.h"
#include "base/test/task_environment.h"
#include "net/base/net_errors.h"
#include "net/http/http_status_code.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "services/network/public/cpp/url_loader_completion_status.h"
#include "services/network/public/cpp/weak_wrapper_shared_url_loader_factory.h"
#include "services/network/public/mojom/url_response_head.mojom.h"
#include "services/network/test/test_url_loader_factory.h"
#include "services/network/test/test_utils.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace taffy {
namespace {

namespace mojom = core_service::mojom;

constexpr char kWorkerOrigin[] = "https://edge.taffy.test";
constexpr char kMintUrl[] = "https://edge.taffy.test/v1/entitlement/mint";
constexpr char kAccessToken[] = "gotrue-access-token";

// The worker's own response shape, field for field
// (services/ai-gateway/src/routes/mint.ts).
constexpr char kMintBody[] = R"({
  "schema_version": 1,
  "token": "taffy-ent-1-token",
  "token_expires_at": 4102444800,
  "plan_id": "plan-standard",
  "window": {"start": 4102444000, "seconds": 3600},
  "requests": {"used": 1, "limit": 10},
  "spend_micro_usd": {"used": 18000, "cap": 1000000},
  "credits": {"granted": 1000, "remaining": 964, "grant":
      "e2a5c7b1-4d3f-4a6e-8b2c-9f0d1e2a3b4c", "unit_micro_usd": 500,
      "next_turn_at": "2026-09-01T00:00:00Z"},
  "max_output_tokens": 2048,
  "models": ["stub-primary", "stub-backup"],
  "valid_until": null
})";

class ProfileEntitlementCacheTest : public testing::Test {
 protected:
  void SetUp() override {
    shared_factory_ =
        base::MakeRefCounted<network::WeakWrapperSharedURLLoaderFactory>(
            &factory_);
    cache_ = std::make_unique<ProfileEntitlementCache>(
        shared_factory_, kWorkerOrigin,
        base::BindLambdaForTesting(
            [this](base::OnceCallback<void(std::optional<std::string>)>
                       reply) {
              ++sessions_asked_;
              std::move(reply).Run(session_token_);
            }));
    factory_.SetInterceptor(base::BindLambdaForTesting(
        [this](const network::ResourceRequest& request) {
          observed_ = request;
          observed_body_ = network::GetUploadData(request);
        }));
  }

  mojom::EntitlementSummaryResultPtr Fetch() {
    mojom::EntitlementSummaryResultPtr summary;
    bool answered = false;
    cache_->FetchSummary(base::BindLambdaForTesting(
        [&](mojom::EntitlementSummaryResultPtr result) {
          answered = true;
          summary = std::move(result);
        }));
    task_environment_.RunUntilIdle();
    EXPECT_TRUE(answered);
    return summary;
  }

  std::optional<std::string> Acquire(bool evict) {
    std::optional<std::string> token;
    bool answered = false;
    cache_->AcquireToken(
        evict, base::BindLambdaForTesting(
                   [&](std::optional<std::string> minted) {
                     answered = true;
                     token = std::move(minted);
                   }));
    task_environment_.RunUntilIdle();
    EXPECT_TRUE(answered);
    return token;
  }

  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::MainThreadType::IO};
  network::TestURLLoaderFactory factory_;
  scoped_refptr<network::SharedURLLoaderFactory> shared_factory_;
  std::unique_ptr<ProfileEntitlementCache> cache_;

  std::optional<std::string> session_token_ = std::string(kAccessToken);
  int sessions_asked_ = 0;
  std::optional<network::ResourceRequest> observed_;
  std::string observed_body_;
};

TEST_F(ProfileEntitlementCacheTest, AMintParsesTheSummaryAndKeepsTheToken) {
  factory_.AddResponse(kMintUrl, kMintBody);

  mojom::EntitlementSummaryResultPtr summary = Fetch();
  ASSERT_TRUE(summary);
  EXPECT_FALSE(summary->definitive_absent);
  EXPECT_EQ(summary->plan_id, "plan-standard");
  EXPECT_EQ(summary->model_ids,
            (std::vector<std::string>{"stub-primary", "stub-backup"}));
  EXPECT_EQ(summary->window_seconds, 3600u);
  EXPECT_EQ(summary->requests_remaining, 9u);
  EXPECT_EQ(summary->credits_granted, 1000u);
  EXPECT_EQ(summary->credits_remaining, 964u);
  EXPECT_EQ(summary->credit_unit_micros, 500u);
  // 2026-09-01T00:00:00Z.
  EXPECT_EQ(summary->next_renewal_epoch_seconds, 1788220800u);
  // null valid_until is the contract's own "open-ended".
  EXPECT_EQ(summary->valid_until_epoch_seconds, 0u);
  EXPECT_EQ(summary->worker_host, "edge.taffy.test");
  EXPECT_EQ(summary->gateway_host, "edge.taffy.test");
  EXPECT_GT(summary->minted_at_utc_ms, 0u);

  // The token stayed here. Nothing in the summary can carry it.
  EXPECT_TRUE(cache_->has_unspent_token_for_testing());

  // The request itself: the session's bearer, the compiled route, no
  // cookies, no redirects, an empty JSON body.
  ASSERT_TRUE(observed_);
  EXPECT_EQ(observed_->url, GURL(kMintUrl));
  EXPECT_EQ(observed_->method, "POST");
  EXPECT_EQ(observed_->headers.GetHeader("authorization"),
            std::optional<std::string>(std::string("Bearer ") + kAccessToken));
  EXPECT_EQ(observed_->credentials_mode, network::mojom::CredentialsMode::kOmit);
  EXPECT_EQ(observed_->redirect_mode, network::mojom::RedirectMode::kError);
  EXPECT_EQ(observed_body_, "{}");
}

TEST_F(ProfileEntitlementCacheTest, AHeldTokenIsSpentExactlyOnce) {
  factory_.AddResponse(kMintUrl, kMintBody);
  ASSERT_TRUE(Fetch());
  ASSERT_TRUE(cache_->has_unspent_token_for_testing());

  // The pre-minted token serves without a second request.
  factory_.ClearResponses();
  EXPECT_EQ(Acquire(false), std::optional<std::string>("taffy-ent-1-token"));
  EXPECT_FALSE(cache_->has_unspent_token_for_testing());

  // The next acquisition has nothing held and mints again.
  factory_.AddResponse(kMintUrl, kMintBody);
  EXPECT_EQ(Acquire(false), std::optional<std::string>("taffy-ent-1-token"));
  EXPECT_EQ(sessions_asked_, 2);
}

TEST_F(ProfileEntitlementCacheTest, EvictingMintsFreshEvenOverAHeldToken) {
  factory_.AddResponse(kMintUrl, kMintBody);
  ASSERT_TRUE(Fetch());
  ASSERT_TRUE(cache_->has_unspent_token_for_testing());

  EXPECT_EQ(Acquire(true), std::optional<std::string>("taffy-ent-1-token"));
  // One mint for the fetch, one forced by the eviction.
  EXPECT_EQ(sessions_asked_, 2);
}

TEST_F(ProfileEntitlementCacheTest, ADefinitiveAbsenceIsASummaryNotAFailure) {
  factory_.AddResponse(kMintUrl, R"({"error":{"code":"no_entitlement"}})",
                       net::HTTP_FORBIDDEN);

  mojom::EntitlementSummaryResultPtr summary = Fetch();
  ASSERT_TRUE(summary);
  EXPECT_TRUE(summary->definitive_absent);
  EXPECT_FALSE(cache_->has_unspent_token_for_testing());

  // A dispatch during a definitive absence is answered, not looped: minting
  // again buys the same answer, so nothing is handed out.
  EXPECT_EQ(Acquire(false), std::nullopt);
}

TEST_F(ProfileEntitlementCacheTest, AnUnheardWorkerFailsWithoutInstalling) {
  const struct {
    net::HttpStatusCode status;
    const char* body;
  } kCases[] = {
      {net::HTTP_UNAUTHORIZED, R"({"error":{"code":"account_token_invalid"}})"},
      {net::HTTP_TOO_MANY_REQUESTS, R"({"error":{"code":"mint_rate_limited"}})"},
      {net::HTTP_SERVICE_UNAVAILABLE,
       R"({"error":{"code":"entitlement_unknown"}})"},
  };
  for (const auto& expected : kCases) {
    factory_.ClearResponses();
    factory_.AddResponse(kMintUrl, expected.body, expected.status);
    EXPECT_FALSE(Fetch()) << expected.status;
    EXPECT_FALSE(cache_->has_unspent_token_for_testing()) << expected.status;
  }
}

TEST_F(ProfileEntitlementCacheTest, AMalformedMintBodyInstallsNothing) {
  for (const char* body : {
           "not json at all",
           R"({"schema_version": 2, "token": "t"})",
           R"({"schema_version": 1})",
           R"({"schema_version": 1, "token": "t", "token_expires_at": 1,
               "plan_id": "p", "window": {"seconds": 0}, "requests": {},
               "credits": {}, "models": []})",
       }) {
    factory_.ClearResponses();
    factory_.AddResponse(kMintUrl, body);
    EXPECT_FALSE(Fetch()) << body;
    EXPECT_FALSE(cache_->has_unspent_token_for_testing()) << body;
  }
}

TEST_F(ProfileEntitlementCacheTest, NoUsableSessionAsksNothingOfTheWorker) {
  session_token_ = std::nullopt;
  EXPECT_FALSE(Fetch());
  EXPECT_EQ(factory_.NumPending(), 0);
  EXPECT_FALSE(observed_);
}

TEST_F(ProfileEntitlementCacheTest, ADisabledOriginFailsEveryMintClosed) {
  cache_ = std::make_unique<ProfileEntitlementCache>(
      shared_factory_, std::string(),
      base::BindLambdaForTesting(
          [](base::OnceCallback<void(std::optional<std::string>)> reply) {
            std::move(reply).Run(std::string("never-used"));
          }));
  EXPECT_FALSE(Fetch());
  EXPECT_EQ(Acquire(false), std::nullopt);
  EXPECT_FALSE(observed_);
}

TEST_F(ProfileEntitlementCacheTest, OneMintServesEveryConcurrentWaiter) {
  factory_.AddResponse(kMintUrl, kMintBody);
  int answers = 0;
  mojom::EntitlementSummaryResultPtr first;
  std::optional<std::string> token;
  cache_->FetchSummary(base::BindLambdaForTesting(
      [&](mojom::EntitlementSummaryResultPtr result) {
        ++answers;
        first = std::move(result);
      }));
  cache_->AcquireToken(false, base::BindLambdaForTesting(
                                  [&](std::optional<std::string> minted) {
                                    ++answers;
                                    token = std::move(minted);
                                  }));
  task_environment_.RunUntilIdle();
  EXPECT_EQ(answers, 2);
  ASSERT_TRUE(first);
  EXPECT_EQ(first->plan_id, "plan-standard");
  EXPECT_EQ(token, std::optional<std::string>("taffy-ent-1-token"));
  // One session read, one request: the mint was shared.
  EXPECT_EQ(sessions_asked_, 1);
  // And the token went to the dispatch, so nothing unspent remains.
  EXPECT_FALSE(cache_->has_unspent_token_for_testing());
}

TEST_F(ProfileEntitlementCacheTest, ClearingForgetsTheTokenAndFailsTheFlight) {
  factory_.AddResponse(kMintUrl, kMintBody);
  ASSERT_TRUE(Fetch());
  ASSERT_TRUE(cache_->has_unspent_token_for_testing());
  cache_->Clear();
  EXPECT_FALSE(cache_->has_unspent_token_for_testing());

  // A mint in flight when the account goes answers "nothing", and its late
  // response installs nothing.
  bool answered = false;
  mojom::EntitlementSummaryResultPtr summary;
  factory_.ClearResponses();
  cache_->FetchSummary(base::BindLambdaForTesting(
      [&](mojom::EntitlementSummaryResultPtr result) {
        answered = true;
        summary = std::move(result);
      }));
  cache_->Clear();
  EXPECT_TRUE(answered);
  EXPECT_FALSE(summary);
  task_environment_.RunUntilIdle();
  EXPECT_FALSE(cache_->has_unspent_token_for_testing());
}

TEST_F(ProfileEntitlementCacheTest,
       CancellingAGenerationDropsMintWaitersWithoutClearingAccountState) {
  factory_.AddResponse(kMintUrl, kMintBody);
  ASSERT_TRUE(Fetch());
  ASSERT_TRUE(cache_->has_unspent_token_for_testing());

  factory_.ClearResponses();
  bool answered = false;
  cache_->FetchSummary(base::BindLambdaForTesting(
      [&](mojom::EntitlementSummaryResultPtr) { answered = true; }));
  cache_->CancelPendingMint();
  task_environment_.RunUntilIdle();

  EXPECT_FALSE(answered);
  EXPECT_TRUE(cache_->has_unspent_token_for_testing());
}

}  // namespace
}  // namespace taffy

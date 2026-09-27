// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <optional>
#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/test/bind.h"
#include "net/http/http_status_code.h"
#include "taffy/browser/model/profile_model_broker_test_support.h"
#include "url/gurl.h"

namespace taffy {
namespace {

namespace mojom = core_service::mojom;
using model_broker_test::CompletionOf;
using model_broker_test::ManagedEffect;
using model_broker_test::ProfileModelBrokerTest;

// The managed half of the suite. The token provider stands in for the
// entitlement cache; what these tests pin is the broker's own conduct — which
// origin it will speak to, what may ride the request, and what one refusal
// buys.
class ProfileModelBrokerManagedTest : public ProfileModelBrokerTest {
 protected:
  void SetUp() override {
    ProfileModelBrokerTest::SetUp();
    InstallTokenProvider();
    broker_->SetQuotaRefusedCallback(
        base::BindLambdaForTesting([this]() { ++quota_refused_count_; }));
  }

  void InstallTokenProvider() {
    broker_->SetEntitlementTokenProvider(base::BindLambdaForTesting(
        [this](bool evict,
               ProfileModelBroker::EntitlementTokenCallback callback) {
          ++tokens_asked_;
          evictions_.push_back(evict);
          if (on_token_asked_) {
            on_token_asked_.Run();
          }
          std::move(callback).Run(next_token_);
        }));
  }

  int tokens_asked_ = 0;
  std::vector<bool> evictions_;
  std::optional<std::string> next_token_ = std::string("minted-token-1");
  base::RepeatingClosure on_token_asked_;
  int quota_refused_count_ = 0;
};

TEST_F(ProfileModelBrokerManagedTest,
       AManagedCallRidesAMintedBearerAndNeverTheStore) {
  factory_.AddResponse("https://edge.taffy.test/v1/messages",
                       R"({"schema_version":1})");

  const mojom::EffectResultPtr& result = Run(ManagedEffect());
  ASSERT_TRUE(result);
  EXPECT_EQ(result->status, mojom::EffectStatus::kCompleted);
  EXPECT_TRUE(CompletionOf(result).empty());
  EXPECT_TRUE(result->model->streamed);
  EXPECT_EQ(streamed_body_, R"({"schema_version":1})");
  EXPECT_EQ(tokens_asked_, 1);
  ASSERT_EQ(evictions_.size(), 1u);
  EXPECT_FALSE(evictions_[0]);
  // The store was never asked: a managed bearer is minted, not resolved.
  EXPECT_TRUE(asked_handle_.empty());
  ASSERT_TRUE(observed_);
  EXPECT_EQ(observed_->url, GURL("https://edge.taffy.test/v1/messages"));
  EXPECT_EQ(observed_->headers.GetHeader("authorization"),
            std::optional<std::string>("Bearer minted-token-1"));
}

TEST_F(ProfileModelBrokerManagedTest,
       AManagedEffectOffTheCompiledOriginIsRefusedWhole) {
  mojom::EffectEnvelopePtr effect = ManagedEffect();
  effect->model_request->endpoint = "https://provider.taffy.test";

  const mojom::EffectResultPtr& result = Run(std::move(effect));
  ASSERT_TRUE(result);
  EXPECT_EQ(result->status, mojom::EffectStatus::kDenied);
  EXPECT_EQ(factory_.NumPending(), 0);
  // Refused before a token existed to lose.
  EXPECT_EQ(tokens_asked_, 0);
}

TEST_F(ProfileModelBrokerManagedTest,
       AManagedEffectNamingAStoreHandleIsRefusedWhole) {
  mojom::EffectEnvelopePtr effect = ManagedEffect();
  effect->model_request->credential_handle = "credential-handle-1";

  const mojom::EffectResultPtr& result = Run(std::move(effect));
  ASSERT_TRUE(result);
  EXPECT_EQ(result->status, mojom::EffectStatus::kDenied);
  EXPECT_EQ(factory_.NumPending(), 0);
  EXPECT_EQ(tokens_asked_, 0);
  EXPECT_TRUE(asked_handle_.empty());
}

TEST_F(ProfileModelBrokerManagedTest, NoTokenSourceIsUnavailableNotDenied) {
  broker_->SetEntitlementTokenProvider(
      ProfileModelBroker::EntitlementTokenProvider());
  const mojom::EffectResultPtr& result = Run(ManagedEffect());
  ASSERT_TRUE(result);
  // What is missing is the browser's own seam, not the person's entitlement.
  EXPECT_EQ(result->status, mojom::EffectStatus::kUnavailable);
}

TEST_F(ProfileModelBrokerManagedTest, ARefusedMintIsUnavailableNotDenied) {
  next_token_ = std::nullopt;
  const mojom::EffectResultPtr& result = Run(ManagedEffect());
  ASSERT_TRUE(result);
  EXPECT_EQ(result->status, mojom::EffectStatus::kUnavailable);
  EXPECT_EQ(factory_.NumPending(), 0);
}

TEST_F(ProfileModelBrokerManagedTest, AStaleTokenBuysExactlyOneFreshRetry) {
  factory_.AddResponse("https://edge.taffy.test/v1/messages",
                       R"({"error":{"code":"unauthorized"}})",
                       net::HTTP_UNAUTHORIZED);
  // The second mint is the moment to let the worker start answering: swap
  // the canned refusal for an answer, and hand out a distinct token so the
  // retry provably rides the fresh one.
  on_token_asked_ = base::BindLambdaForTesting([this]() {
    if (tokens_asked_ == 2) {
      next_token_ = std::string("minted-token-2");
      factory_.ClearResponses();
      factory_.AddResponse("https://edge.taffy.test/v1/messages",
                           R"({"schema_version":1})");
    }
  });

  const mojom::EffectResultPtr& result = Run(ManagedEffect());
  ASSERT_TRUE(result);
  EXPECT_EQ(result->status, mojom::EffectStatus::kCompleted);
  EXPECT_EQ(tokens_asked_, 2);
  ASSERT_EQ(evictions_.size(), 2u);
  EXPECT_FALSE(evictions_[0]);
  // The retry evicts: the held state bought the refusal.
  EXPECT_TRUE(evictions_[1]);
  ASSERT_TRUE(observed_);
  EXPECT_EQ(observed_->headers.GetHeader("authorization"),
            std::optional<std::string>("Bearer minted-token-2"));
  EXPECT_EQ(quota_refused_count_, 0);
}

TEST_F(ProfileModelBrokerManagedTest,
       ASettledQuotaRefusalIsDeniedAndPokesTheRefresh) {
  factory_.AddResponse("https://edge.taffy.test/v1/messages",
                       R"({"error":{"code":"quota_denied"}})",
                       net::HTTP_PAYMENT_REQUIRED);

  const mojom::EffectResultPtr& result = Run(ManagedEffect());
  ASSERT_TRUE(result);
  // Two mints — the arrival and the one free retry — and then the worker's
  // settled answer, mapped like every other refusal about what has to
  // change before asking again.
  EXPECT_EQ(result->status, mojom::EffectStatus::kDenied);
  EXPECT_EQ(tokens_asked_, 2);
  EXPECT_EQ(quota_refused_count_, 1);
  // A refusal body cannot become partial assistant prose, including across
  // the fresh-token retry.
  EXPECT_TRUE(CompletionOf(result).empty());
  EXPECT_TRUE(streamed_body_.empty());
}

}  // namespace
}  // namespace taffy

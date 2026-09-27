// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/providerauth/provider_access_resolver.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/memory/scoped_refptr.h"
#include "base/run_loop.h"
#include "base/test/bind.h"
#include "content/public/test/browser_task_environment.h"
#include "content/public/test/test_browser_context.h"
#include "services/network/public/cpp/weak_wrapper_shared_url_loader_factory.h"
#include "services/network/test/test_url_loader_factory.h"
#include "taffy/browser/account/fake_profile_platform_adapter.h"
#include "taffy/browser/account/profile_account_broker.h"
#include "taffy/browser/providerauth/profile_provider_auth_broker.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

// The resolve dance (decision 0081): Android decides what a sealed record is
// right now; this class carries the one refresh a stale record buys and
// files the state the roster should show. The observable facts are the
// material answered, the refresh legs actually posted, what was stored back,
// and the flow events Android would file.

namespace taffy {
namespace {

namespace account_mojom = browser::account::mojom;

constexpr char kXaiTokenUrl[] = "https://auth.x.ai/oauth2/token";

account_mojom::ProviderAccessResultPtr Access(
    account_mojom::ProviderAccessKind kind, const std::string &material) {
  auto result = account_mojom::ProviderAccessResult::New();
  result->kind = kind;
  result->material.assign(material.begin(), material.end());
  return result;
}

class ProviderAccessResolverTest : public testing::Test {
 protected:
  ProviderAccessResolverTest() {
    account_ = std::make_unique<ProfileAccountBroker>(&browser_context_);
    account_->BindPlatformAdapter(adapter_.BindNewPipeAndPassRemote());
    broker_ = std::make_unique<ProfileProviderAuthBroker>(
        base::MakeRefCounted<network::WeakWrapperSharedURLLoaderFactory>(
            &network_),
        account_.get());
    resolver_ = std::make_unique<ProviderAccessResolver>(account_.get(),
                                                         broker_.get());
  }

  // Resolves and settles every mojo and network hop the dance takes.
  std::optional<std::string> Resolve(const std::string &provider_id) {
    std::optional<std::optional<std::string>> answer;
    resolved_host_.reset();
    resolver_->Resolve(provider_id, provider_id,
                       base::BindLambdaForTesting(
                           [&](std::optional<std::string> material,
                               std::optional<std::string> host) {
                             answer = std::move(material);
                             resolved_host_ = std::move(host);
                           }));
    base::RunLoop().RunUntilIdle();
    EXPECT_TRUE(answer.has_value()) << "the resolver must answer exactly once";
    return answer.value_or(std::nullopt);
  }

  std::vector<account_mojom::ProviderFlowEventKind> EventKinds() const {
    std::vector<account_mojom::ProviderFlowEventKind> kinds;
    for (const account_mojom::ProviderFlowEventPtr &event : adapter_.events()) {
      kinds.push_back(event->kind);
    }
    return kinds;
  }

  content::BrowserTaskEnvironment task_environment_;
  content::TestBrowserContext browser_context_;
  FakeProfilePlatformAdapter adapter_;
  std::unique_ptr<ProfileAccountBroker> account_;
  network::TestURLLoaderFactory network_;
  std::unique_ptr<ProfileProviderAuthBroker> broker_;
  std::unique_ptr<ProviderAccessResolver> resolver_;
  // The address the last resolve answered with, for the one vendor shape that
  // ever carries one.
  std::optional<std::string> resolved_host_;
};

TEST_F(ProviderAccessResolverTest, ARawKeyPassesThroughUntouched) {
  adapter_.EnqueueAccessResult(
      Access(account_mojom::ProviderAccessKind::kRawKey, "sk-raw-1"));
  EXPECT_EQ("sk-raw-1", Resolve("anthropic").value_or(""));
  EXPECT_TRUE(adapter_.events().empty());
}

TEST_F(ProviderAccessResolverTest, AFreshAccessTokenIsTheMaterial) {
  adapter_.EnqueueAccessResult(
      Access(account_mojom::ProviderAccessKind::kAccessToken, "at-fresh"));
  EXPECT_EQ("at-fresh", Resolve("xai").value_or(""));
  // No address, because this vendor's credential carries none: the request
  // goes where the catalog says, as it does for every vendor but one.
  EXPECT_FALSE(resolved_host_.has_value());
  EXPECT_TRUE(adapter_.events().empty());
}

// The one vendor whose exchange issues an address with its token: the answer
// carries both, and they arrive together because a destination without the
// credential it belongs to is a place a later call could be sent on a
// credential this answer could not produce.
TEST_F(ProviderAccessResolverTest, AnIssuedAddressTravelsWithItsToken) {
  account_mojom::ProviderAccessResultPtr result =
      Access(account_mojom::ProviderAccessKind::kAccessToken, "at-copilot");
  result->credential_host = "https://one.api.vendor.example";
  adapter_.EnqueueAccessResult(std::move(result));
  EXPECT_EQ("at-copilot", Resolve("github-copilot").value_or(""));
  EXPECT_EQ(std::optional<std::string>("https://one.api.vendor.example"),
            resolved_host_);
}

TEST_F(ProviderAccessResolverTest, ANullAnswerIsARefusalWithNothingFiled) {
  // The fake's empty queue answers null: the not-configured shape.
  EXPECT_FALSE(Resolve("xai").has_value());
  EXPECT_TRUE(adapter_.events().empty());
}

TEST_F(ProviderAccessResolverTest, SignInRequiredFilesTheFactAndRefuses) {
  adapter_.EnqueueAccessResult(
      Access(account_mojom::ProviderAccessKind::kSignInRequired, ""));
  EXPECT_FALSE(Resolve("xai").has_value());
  // The roster must flip instead of reading USABLE over denied calls.
  ASSERT_EQ(1u, adapter_.events().size());
  EXPECT_EQ(account_mojom::ProviderFlowEventKind::kRefreshSignInRequired,
            adapter_.events()[0]->kind);
  EXPECT_EQ("xai", adapter_.events()[0]->provider_id);
  EXPECT_TRUE(adapter_.events()[0]->flow_id.empty());
}

TEST_F(ProviderAccessResolverTest, AStaleRecordBuysExactlyOneRefresh) {
  adapter_.EnqueueAccessResult(
      Access(account_mojom::ProviderAccessKind::kRefreshRequired, "rt-old"));
  adapter_.EnqueueAccessResult(
      Access(account_mojom::ProviderAccessKind::kAccessToken, "at-rotated"));
  network_.AddResponse(kXaiTokenUrl,
                       R"({"access_token":"at-rotated",)"
                       R"("refresh_token":"rt-new","expires_in":3600})");

  EXPECT_EQ("at-rotated", Resolve("xai").value_or(""));

  // The rotation was sealed before anything answered with it, and it was
  // named a rotation so a forget race drops it instead of resurrecting it.
  ASSERT_EQ(1u, adapter_.stored_records().size());
  EXPECT_EQ("xai", adapter_.stored_records()[0].provider_id);
  EXPECT_TRUE(adapter_.stored_records()[0].rotation);
  EXPECT_EQ(std::vector<uint8_t>({'a', 't', '-', 'r', 'o', 't', 'a', 't', 'e',
                                  'd'}),
            adapter_.stored_records()[0].record->access_token);
  EXPECT_TRUE(adapter_.events().empty());
}

TEST_F(ProviderAccessResolverTest, AStillStaleAnswerAfterRotationRefuses) {
  adapter_.EnqueueAccessResult(
      Access(account_mojom::ProviderAccessKind::kRefreshRequired, "rt-old"));
  adapter_.EnqueueAccessResult(
      Access(account_mojom::ProviderAccessKind::kRefreshRequired, "rt-new"));
  network_.AddResponse(kXaiTokenUrl,
                       R"({"access_token":"at-short","expires_in":30})");

  EXPECT_FALSE(Resolve("xai").has_value());

  // One refresh per resolve: refreshing harder cannot make a vendor answer
  // longer expiries, so the second stale answer is a refusal, not a loop.
  EXPECT_EQ(1u, adapter_.stored_records().size());
  ASSERT_EQ(1u, adapter_.events().size());
  EXPECT_EQ(account_mojom::ProviderFlowEventKind::kRefreshFailed,
            adapter_.events()[0]->kind);
}

TEST_F(ProviderAccessResolverTest, AnInvalidGrantRefreshFilesSignInRequired) {
  adapter_.EnqueueAccessResult(
      Access(account_mojom::ProviderAccessKind::kRefreshRequired, "rt-dead"));
  network_.AddResponse(kXaiTokenUrl, R"({"error":"invalid_grant"})");

  EXPECT_FALSE(Resolve("xai").has_value());

  EXPECT_TRUE(adapter_.stored_records().empty());
  ASSERT_EQ(1u, adapter_.events().size());
  EXPECT_EQ(account_mojom::ProviderFlowEventKind::kRefreshSignInRequired,
            adapter_.events()[0]->kind);
}

TEST_F(ProviderAccessResolverTest, ATransientRefreshFailureFilesRefreshFailed) {
  adapter_.EnqueueAccessResult(
      Access(account_mojom::ProviderAccessKind::kRefreshRequired, "rt-maybe"));
  network_.AddResponse(kXaiTokenUrl, "not json");

  EXPECT_FALSE(Resolve("xai").has_value());

  ASSERT_EQ(1u, adapter_.events().size());
  EXPECT_EQ(account_mojom::ProviderFlowEventKind::kRefreshFailed,
            adapter_.events()[0]->kind);
}

TEST_F(ProviderAccessResolverTest, ARotationLostToAForgetStaysQuiet) {
  adapter_.EnqueueAccessResult(
      Access(account_mojom::ProviderAccessKind::kRefreshRequired, "rt-old"));
  network_.AddResponse(kXaiTokenUrl,
                       R"({"access_token":"at-new","expires_in":3600})");
  adapter_.set_store_answers(false);

  EXPECT_FALSE(Resolve("xai").has_value());

  // The person removed the credential mid-refresh and the person wins: no
  // state is filed for a provider that no longer has one.
  EXPECT_TRUE(adapter_.stored_records().empty());
  EXPECT_TRUE(adapter_.events().empty());
}

}  // namespace
}  // namespace taffy

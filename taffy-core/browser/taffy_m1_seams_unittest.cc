// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "content/public/test/browser_task_environment.h"
#include "taffy/browser/taffy_download_intent_router.h"
#include "taffy/common/public/taffy_product_identity.h"
#include "testing/gtest/include/gtest/gtest.h"

// The M1 seams have one rule each, and each rule is the kind that decays into
// a comment nobody enforces unless a test holds it down.

namespace taffy {
namespace {

class StubIntentDelegate : public DownloadIntentDelegate {
 public:
  bool CanHandleExternally(const std::string& target_scheme) override {
    return target_scheme == "mailto";
  }
};

class TaffyM1SeamsTest : public testing::Test {
 protected:
  void TearDown() override { GetDownloadIntentRouter().SetDelegate(nullptr); }

  content::BrowserTaskEnvironment task_environment_;
};

TEST_F(TaffyM1SeamsTest, ProductIdentityNamesTheProductAndNothingElse) {
  const ProductIdentity& identity = GetProductIdentity();
  EXPECT_FALSE(identity.product_name.empty());
  EXPECT_TRUE(identity.is_taffy_branded);
  // Decision 0130: the token stays empty and the product names itself in the
  // client-hint brand list instead, so nothing may reach a request header here.
  // product_user_agent_brand_unittest.cc holds the other half.
  EXPECT_TRUE(identity.user_agent_product_token.empty());
  // Decision 0206: About opens this address through the one entry point that
  // takes none (TaffyAttributionNoticeBridge), so it has to be the engine's
  // generated notice and nothing else.
  EXPECT_EQ(identity.attribution_resource_path, "chrome://credits");
}

TEST_F(TaffyM1SeamsTest, AssistantInitiatedDownloadIsRefused) {
  DownloadRequestFacts facts;
  facts.tab_id = TabId{"tab_1"};
  facts.initiator = NavigationInitiator::kAssistant;
  facts.has_user_activation = true;  // Does not help. Initiator is the rule.

  EXPECT_EQ(DownloadDecision::kRefusedAssistantInitiated,
            GetDownloadIntentRouter().EvaluateDownload(facts));
}

TEST_F(TaffyM1SeamsTest,
       AssistantDownloadRequiresEveryBrowserOwnedAuthorityFact) {
  DownloadRequestFacts facts;
  facts.tab_id = TabId{"tab_1"};
  facts.initiator = NavigationInitiator::kAssistant;
  facts.task_id = TaskId{"task_1"};
  facts.actor_lease_id = ActorLeaseId{"lease_1"};

  EXPECT_EQ(DownloadDecision::kRefusedNoCapability,
            GetDownloadIntentRouter().EvaluateDownload(facts));

  facts.capability_reference = CapabilityReference{"capability_1"};
  facts.dispatch_id = DispatchId{"dispatch_1"};
  facts.capability_admitted = true;
  EXPECT_EQ(DownloadDecision::kAllowOrdinaryPath,
            GetDownloadIntentRouter().EvaluateDownload(facts));
}

TEST_F(TaffyM1SeamsTest, UnattributedDownloadIsRefused) {
  DownloadRequestFacts facts;
  facts.tab_id = TabId{"tab_1"};
  facts.initiator = NavigationInitiator::kUnknown;

  EXPECT_EQ(DownloadDecision::kRefusedUnattributedInitiator,
            GetDownloadIntentRouter().EvaluateDownload(facts));
}

TEST_F(TaffyM1SeamsTest,
       ContentInitiatedDownloadWithoutAWebContentsFailsClosed) {
  EXPECT_TRUE(ShouldRefuseContentInitiatedDownload(nullptr));
}

TEST_F(TaffyM1SeamsTest, UserInitiatedDownloadTakesTheOrdinaryPath) {
  DownloadRequestFacts facts;
  facts.tab_id = TabId{"tab_1"};
  facts.initiator = NavigationInitiator::kUser;
  facts.has_user_activation = true;

  EXPECT_EQ(DownloadDecision::kAllowOrdinaryPath,
            GetDownloadIntentRouter().EvaluateDownload(facts));
}

TEST_F(TaffyM1SeamsTest, AssistantInitiatedExternalIntentIsRefused) {
  StubIntentDelegate delegate;
  GetDownloadIntentRouter().SetDelegate(&delegate);

  ExternalIntentFacts facts;
  facts.tab_id = TabId{"tab_1"};
  facts.initiator = NavigationInitiator::kAssistant;
  facts.target_scheme = "mailto";

  EXPECT_EQ(ExternalIntentDecision::kRefusedAssistantInitiated,
            GetDownloadIntentRouter().EvaluateExternalNavigation(facts));
}

TEST_F(TaffyM1SeamsTest, UnhandleableSchemeStaysInTheBrowser) {
  StubIntentDelegate delegate;
  GetDownloadIntentRouter().SetDelegate(&delegate);

  ExternalIntentFacts facts;
  facts.tab_id = TabId{"tab_1"};
  facts.initiator = NavigationInitiator::kUser;
  facts.target_scheme = "https";

  // Not a block: the navigation still happens, it just happens here.
  EXPECT_EQ(ExternalIntentDecision::kKeepInBrowser,
            GetDownloadIntentRouter().EvaluateExternalNavigation(facts));
}

}  // namespace
}  // namespace taffy

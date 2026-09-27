// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "taffy/browser/core_api/saved_flow_start_navigation.h"

#include "base/test/test_future.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/navigation_simulator.h"
#include "content/public/test/test_renderer_host.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {
namespace service = core_service::mojom;

service::SavedFlowReviewPtr Review(const std::string& address) {
  auto flow = service::SavedFlowReview::New();
  flow->origin = "https://example.test";
  flow->provenance = service::SkillProvenance::kRecordedFromTask;
  flow->status = service::SkillStatus::kActive;
  flow->recorded_from_task_id = "completed-task";
  flow->step_count = 1u;
  auto step = service::SkillObservedStep::New();
  step->verb = "browser.navigate";
  auto argument = service::SkillObservedArgument::New();
  argument->kind = service::SkillArgumentKind::kPublicAddress;
  argument->public_address = address;
  step->arguments.push_back(std::move(argument));
  flow->reviewed_steps.push_back(std::move(step));
  return flow;
}

TEST(SavedFlowStartAddressTest, OnlyTheCurrentRecordedPublicOriginCanNavigate) {
  EXPECT_EQ(
      ReviewedSavedFlowStartAddress(*Review("https://example.test/start")),
      GURL("https://example.test/start"));
  for (const char* address :
       {"http://example.test/start", "https://other.test/start",
        "https://example.test/start?token=private",
        "https://example.test/start#private",
        "https://user:secret@example.test/start", "javascript:alert(1)"}) {
    EXPECT_FALSE(ReviewedSavedFlowStartAddress(*Review(address)));
  }
  auto disabled = Review("https://example.test/start");
  disabled->status = service::SkillStatus::kDisabled;
  EXPECT_FALSE(ReviewedSavedFlowStartAddress(*disabled));
  auto partial = Review("https://example.test/start");
  partial->step_count = 2u;
  EXPECT_FALSE(ReviewedSavedFlowStartAddress(*partial));
  auto authored = Review("https://example.test/start");
  authored->provenance = service::SkillProvenance::kAuthored;
  EXPECT_FALSE(ReviewedSavedFlowStartAddress(*authored));
}

class SavedFlowStartNavigationTest : public content::RenderViewHostTestHarness {
};

TEST_F(SavedFlowStartNavigationTest,
       ReportsOnlyAfterTheExactNavigationCommits) {
  base::test::TestFuture<bool> completed;
  NavigateSavedFlowStart(web_contents(), GURL("https://example.test/start"),
                         completed.GetCallback());
  EXPECT_FALSE(completed.IsReady());
  auto navigation = content::NavigationSimulator::CreateFromPending(
      web_contents()->GetController());
  ASSERT_TRUE(navigation);
  navigation->Commit();
  EXPECT_TRUE(completed.Get());
}

TEST_F(SavedFlowStartNavigationTest,
       ARedirectCannotStandInForTheReviewedStart) {
  base::test::TestFuture<bool> completed;
  NavigateSavedFlowStart(web_contents(), GURL("https://example.test/start"),
                         completed.GetCallback());
  auto navigation = content::NavigationSimulator::CreateFromPending(
      web_contents()->GetController());
  ASSERT_TRUE(navigation);
  navigation->Redirect(GURL("https://example.test/other"));
  navigation->Commit();
  EXPECT_FALSE(completed.Get());
}

TEST_F(SavedFlowStartNavigationTest, ClosingTheTabRefusesThePendingOpen) {
  base::test::TestFuture<bool> completed;
  NavigateSavedFlowStart(web_contents(), GURL("https://example.test/start"),
                         completed.GetCallback());
  DeleteContents();
  EXPECT_FALSE(completed.Get());
}

TEST_F(SavedFlowStartNavigationTest,
       RedirectingBackDoesNotRestoreTheReviewedStart) {
  base::test::TestFuture<bool> completed;
  NavigateSavedFlowStart(web_contents(), GURL("https://example.test/start"),
                         completed.GetCallback());
  auto navigation = content::NavigationSimulator::CreateFromPending(
      web_contents()->GetController());
  ASSERT_TRUE(navigation);
  navigation->Redirect(GURL("https://example.test/other"));
  navigation->Redirect(GURL("https://example.test/start"));
  navigation->Commit();
  EXPECT_FALSE(completed.Get());
}

TEST_F(SavedFlowStartNavigationTest,
       ANewNavigationToTheSameAddressCannotCompleteTheOpen) {
  const GURL address("https://example.test/start");
  base::test::TestFuture<bool> completed;
  NavigateSavedFlowStart(web_contents(), address, completed.GetCallback());
  auto replacement = content::NavigationSimulator::CreateBrowserInitiated(
      address, web_contents());
  replacement->Start();
  replacement->Commit();
  EXPECT_FALSE(completed.Get());
}
}  // namespace
}  // namespace taffy

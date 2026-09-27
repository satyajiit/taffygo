// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "taffy/browser/core_api/saved_flow_query_projection.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {
namespace api = core_api::mojom;
namespace service = core_service::mojom;

service::SavedFlowQueryResultPtr CompleteReview() {
  auto result = service::SavedFlowQueryResult::New();
  result->operation =
      service::OperationEnvelope::New("operation", 7u, 0u, 100u, "operation");
  result->status = service::SavedFlowQueryStatus::kAvailable;
  auto flow = service::SavedFlowReview::New();
  flow->skill_id = "flow.saved";
  flow->origin = "https://example.test";
  flow->provenance = service::SkillProvenance::kRecordedFromTask;
  flow->status = service::SkillStatus::kActive;
  flow->active_version = 2u;
  flow->step_count = 1u;
  flow->recorded_from_task_id = "completed-task";
  auto step = service::SkillObservedStep::New();
  step->verb = "browser.navigate";
  auto argument = service::SkillObservedArgument::New();
  argument->kind = service::SkillArgumentKind::kPublicAddress;
  argument->public_address = "https://example.test/start";
  step->arguments.push_back(std::move(argument));
  flow->reviewed_steps.push_back(std::move(step));
  result->flows.push_back(std::move(flow));
  return result;
}

TEST(SavedFlowQueryProjectionTest,
     PreservesCompleteVersionAndPublicStartWithoutAnOffer) {
  auto result = ProjectSavedFlowQuery("request", 7u, CompleteReview());
  EXPECT_EQ(result->request_id, "request");
  EXPECT_EQ(result->service_generation, 7u);
  ASSERT_EQ(result->availability, api::SavedFlowQueryAvailability::kAvailable);
  ASSERT_EQ(result->flows.size(), 1u);
  const auto& flow = result->flows.front();
  EXPECT_EQ(flow->active_version, 2u);
  ASSERT_EQ(flow->reviewed_steps.size(), flow->step_count);
  EXPECT_EQ(flow->reviewed_steps[0]->arguments[0]->public_address,
            "https://example.test/start");
}

TEST(SavedFlowQueryProjectionTest,
     AGenerationMismatchNeverReturnsReviewContent) {
  auto result = ProjectSavedFlowQuery("request", 8u, CompleteReview());
  EXPECT_EQ(result->availability,
            api::SavedFlowQueryAvailability::kUnavailable);
  EXPECT_TRUE(result->flows.empty());
}

TEST(SavedFlowQueryProjectionTest, APartialReviewWithdrawsTheWholeResult) {
  auto reply = CompleteReview();
  reply->flows.front()->step_count = 2u;
  auto result = ProjectSavedFlowQuery("request", 7u, std::move(reply));
  EXPECT_EQ(result->availability,
            api::SavedFlowQueryAvailability::kInvalidRequest);
  EXPECT_TRUE(result->flows.empty());
}

TEST(SavedFlowQueryProjectionTest, ARefusalCannotCarryAnOtherwiseValidReview) {
  auto reply = CompleteReview();
  reply->status = service::SavedFlowQueryStatus::kPrivateProfile;
  auto result = ProjectSavedFlowQuery("request", 7u, std::move(reply));
  EXPECT_EQ(result->availability,
            api::SavedFlowQueryAvailability::kInvalidRequest);
  EXPECT_TRUE(result->flows.empty());
}

TEST(SavedFlowQueryProjectionTest,
     MalformedMetadataAndArgumentShapesAreWithdrawn) {
  for (int shape = 0; shape < 5; ++shape) {
    auto reply = CompleteReview();
    auto& flow = reply->flows.front();
    if (shape == 0) {
      flow->skill_id.clear();
    }
    if (shape == 1) {
      flow->active_version = 0u;
    }
    if (shape == 2) {
      flow->origin = std::string(api::kMaxSkillOriginBytes + 1u, 'x');
    }
    if (shape == 3) {
      flow->reviewed_steps.front()->verb.clear();
    }
    if (shape == 4) {
      flow->reviewed_steps.front()->arguments.front()->public_address.reset();
    }
    auto result = ProjectSavedFlowQuery("request", 7u, std::move(reply));
    EXPECT_EQ(result->availability,
              api::SavedFlowQueryAvailability::kInvalidRequest);
    EXPECT_TRUE(result->flows.empty());
  }
}

TEST(SavedFlowQueryProjectionTest, DuplicateIdentitiesWithdrawTheWholeReply) {
  auto reply = CompleteReview();
  reply->flows.push_back(reply->flows.front()->Clone());
  auto result = ProjectSavedFlowQuery("request", 7u, std::move(reply));
  EXPECT_EQ(result->availability,
            api::SavedFlowQueryAvailability::kInvalidRequest);
  EXPECT_TRUE(result->flows.empty());
}
}  // namespace
}  // namespace taffy

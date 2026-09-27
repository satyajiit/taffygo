// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// One sheet for every field on a page that only the person can supply
// (decision 0238). On myAadhaar the identity number and the CAPTCHA under it
// are in no form, so a sheet asked about the one field the task named and the
// person would have been asked twice; and a count alone left a model turn to
// name the fields the values were for, which on a phone it never did.

#include <string>
#include <vector>

#include "content/public/test/browser_task_environment.h"
#include "taffy/browser/field_value_request_coordinator_test_support.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

using namespace field_value_request_coordinator_test;

constexpr char kCaptchaId[] = "node-9";

TEST_F(FieldValueRequestCoordinatorTest,
       AFieldInNoFormIsAskedAboutWithItsCompanions) {
  node_facts_[kNodeId] = EditableField(kNodeId, Sensitivity::kIdentity);
  node_facts_[kCaptchaId] =
      EditableField(kCaptchaId, Sensitivity::kChallengeResponse);
  coordinator_->OnCoreFieldValueRequest(kRequestId, kTaskId, kTabId, kNodeId,
                                        {kCaptchaId});
  task_environment_->RunUntilIdle();

  ASSERT_EQ(1u, client_.opened.size());
  ASSERT_EQ(2u, client_.opened[0]->fields.size());
  EXPECT_EQ(kNodeId, client_.opened[0]->fields[0]->field_id);
  EXPECT_EQ(kCaptchaId, client_.opened[0]->fields[1]->field_id);

  const SupplyOutcome outcome = Supply({"first-value", "second-value"});
  ASSERT_TRUE(outcome.answered);
  EXPECT_TRUE(outcome.refused.empty());
  ASSERT_EQ(1u, reported_.size());
  EXPECT_EQ(2u, reported_[0].second);
  // Which field each held value is for, in the order the sheet showed them,
  // which is what lets the task put them into the page itself.
  ASSERT_EQ(1u, reported_field_node_ids_.size());
  EXPECT_EQ(std::vector<std::string>({kNodeId, kCaptchaId}),
            reported_field_node_ids_[0].second);
}

TEST_F(FieldValueRequestCoordinatorTest,
       ACompanionTheSheetCannotShowIsLeftOffAndTheRestIsAsked) {
  node_facts_[kNodeId] = EditableField(kNodeId, Sensitivity::kIdentity);
  // A CAPTCHA whose picture could not be copied — below the fold, most
  // often — and a companion that no longer takes a value at all.
  ResolvedNodeFacts captcha =
      EditableField(kCaptchaId, Sensitivity::kChallengeResponse);
  captcha.challenge_kind = ChallengeKind::kImage;
  node_facts_[kCaptchaId] = captcha;
  ResolvedNodeFacts read_only = EditableField("node-3", Sensitivity::kIdentity);
  read_only.asserted_states.push_back(NodeState::kReadOnly);
  node_facts_["node-3"] = read_only;
  challenge_presentation_available_ = false;

  coordinator_->OnCoreFieldValueRequest(kRequestId, kTaskId, kTabId, kNodeId,
                                        {kCaptchaId, "node-3"});
  task_environment_->RunUntilIdle();

  // The named field is still asked about, and the request did not end.
  ASSERT_EQ(1u, client_.opened.size());
  ASSERT_EQ(1u, client_.opened[0]->fields.size());
  EXPECT_EQ(kNodeId, client_.opened[0]->fields[0]->field_id);
  EXPECT_TRUE(closed_request_ids_.empty());
  EXPECT_TRUE(reported_.empty());

  const SupplyOutcome outcome = Supply({"first-value"});
  ASSERT_TRUE(outcome.answered);
  ASSERT_EQ(1u, reported_field_node_ids_.size());
  EXPECT_EQ(std::vector<std::string>({kNodeId}),
            reported_field_node_ids_[0].second);
}

TEST_F(FieldValueRequestCoordinatorTest, AFormIsAskedAboutAsItsOwnFields) {
  // A named form brings its own fields, so companions are not added to it: a
  // sheet over a form is that form, as decision 0088 drew it.
  ResolvedNodeFacts form;
  form.node_id = SemanticNodeId{kNodeId};
  form.observed_at_revision = 12u;
  form.form_field_node_ids = {SemanticNodeId{"node-1"},
                              SemanticNodeId{"node-2"}};
  node_facts_[kNodeId] = form;
  node_facts_["node-1"] = EditableField("node-1", Sensitivity::kIdentity);
  node_facts_["node-2"] = EditableField("node-2", Sensitivity::kOneTimeCode);
  node_facts_[kCaptchaId] =
      EditableField(kCaptchaId, Sensitivity::kChallengeResponse);

  coordinator_->OnCoreFieldValueRequest(kRequestId, kTaskId, kTabId, kNodeId,
                                        {kCaptchaId});
  task_environment_->RunUntilIdle();

  ASSERT_EQ(1u, client_.opened.size());
  ASSERT_EQ(2u, client_.opened[0]->fields.size());
  EXPECT_EQ("node-1", client_.opened[0]->fields[0]->field_id);
  EXPECT_EQ("node-2", client_.opened[0]->fields[1]->field_id);
}

// A block with no fields of its own is asked about as the fields the task
// named beside it (decision 0243). On myAadhaar the model named a block the
// fields are drawn in, which is no form element, and this path answered "not
// a field" twice while the identity number and the CAPTCHA were on the page.
TEST_F(FieldValueRequestCoordinatorTest,
       ABlockWithNoFieldsIsAskedAboutAsTheFieldsBesideIt) {
  ResolvedNodeFacts block;
  block.node_id = SemanticNodeId{kNodeId};
  block.observed_at_revision = 12u;
  node_facts_[kNodeId] = block;
  node_facts_["node-1"] = EditableField("node-1", Sensitivity::kIdentity);
  node_facts_[kCaptchaId] =
      EditableField(kCaptchaId, Sensitivity::kChallengeResponse);

  coordinator_->OnCoreFieldValueRequest(kRequestId, kTaskId, kTabId, kNodeId,
                                        {"node-1", kCaptchaId});
  task_environment_->RunUntilIdle();

  ASSERT_EQ(1u, client_.opened.size());
  ASSERT_EQ(2u, client_.opened[0]->fields.size());
  EXPECT_EQ("node-1", client_.opened[0]->fields[0]->field_id);
  EXPECT_EQ(kCaptchaId, client_.opened[0]->fields[1]->field_id);

  const SupplyOutcome outcome = Supply({"first-value", "second-value"});
  ASSERT_TRUE(outcome.answered);
  ASSERT_EQ(1u, reported_field_node_ids_.size());
  EXPECT_EQ(std::vector<std::string>({"node-1", kCaptchaId}),
            reported_field_node_ids_[0].second);
}

// With nothing named beside it, a block that is no form is still not a field,
// and no sheet opens.
TEST_F(FieldValueRequestCoordinatorTest,
       ABlockWithNoFieldsAndNothingBesideItIsNotAField) {
  ResolvedNodeFacts block;
  block.node_id = SemanticNodeId{kNodeId};
  block.observed_at_revision = 12u;
  node_facts_[kNodeId] = block;

  coordinator_->OnCoreFieldValueRequest(kRequestId, kTaskId, kTabId, kNodeId,
                                        {});
  task_environment_->RunUntilIdle();

  EXPECT_TRUE(client_.opened.empty());
  ASSERT_EQ(1u, reported_outcomes_.size());
  EXPECT_EQ(core_service::mojom::FieldValueAskOutcome::kNotAField,
            reported_outcomes_[0].second);
}

}  // namespace
}  // namespace taffy

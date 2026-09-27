// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <limits>

#include "taffy/browser/field_value_request_coordinator_test_support.h"
#include "testing/gtest/include/gtest/gtest.h"

// The one "Approve once" a complete answer leaves behind, spent field by
// field. Every non-value fact is compared: the task, the tab, the site, the
// frame, the document, the exact field and its place in the order. The page's
// revision may move forward within that document, because the task's own
// first fill moves it (decision 0194), and nothing else may change.

namespace taffy {
namespace {

namespace surface_mojom = browser::field_values::mojom;
using namespace field_value_request_coordinator_test;

TEST_F(FieldValueRequestCoordinatorTest,
       ExactPreapprovalBindsTaskTabNodeRequestIndexOriginAndPage) {
  OpenRequest();
  ASSERT_EQ(surface_mojom::FieldValueSupplyVerdict::kAccepted,
            Supply({"1234 5678 9012"}).verdict);
  ASSERT_EQ(1u, coordinator_->preapproval_count_for_testing());

  EXPECT_TRUE(coordinator_->ConsumeFormFillPreapproval(
      kTaskId, kTabId, kNodeId, kRequestId, 0u, "https://bank.test", "frame-1",
      "page-1", 12u, 1u, 1u));
  EXPECT_EQ(0u, coordinator_->preapproval_count_for_testing());
  // The same reference position is no longer an approval after first use.
  EXPECT_FALSE(coordinator_->ConsumeFormFillPreapproval(
      kTaskId, kTabId, kNodeId, kRequestId, 0u, "https://bank.test", "frame-1",
      "page-1", 12u, 1u, 1u));
}

TEST_F(FieldValueRequestCoordinatorTest,
       ReorderOrFieldSubstitutionInvalidatesTheRemainingSequence) {
  ResolvedNodeFacts form;
  form.node_id = SemanticNodeId{kNodeId};
  form.observed_at_revision = 12u;
  form.form_field_node_ids = {SemanticNodeId{"field-a"},
                              SemanticNodeId{"field-b"}};
  node_facts_[kNodeId] = form;
  node_facts_["field-a"] = EditableField("field-a", Sensitivity::kPersonal);
  node_facts_["field-b"] = EditableField("field-b", Sensitivity::kAccount);
  OpenRequest();
  ASSERT_EQ(surface_mojom::FieldValueSupplyVerdict::kAccepted,
            Supply({"Alice", "alice@example.test"}).verdict);

  // Index one cannot run before index zero. The mismatch burns the whole
  // authorization, so retrying in the right order is not a bypass.
  EXPECT_FALSE(coordinator_->ConsumeFormFillPreapproval(
      kTaskId, kTabId, "field-b", kRequestId, 1u, "https://bank.test",
      "frame-1", "page-1", 12u, 1u, 1u));
  EXPECT_FALSE(coordinator_->ConsumeFormFillPreapproval(
      kTaskId, kTabId, "field-a", kRequestId, 0u, "https://bank.test",
      "frame-1", "page-1", 12u, 1u, 1u));
}

// A fill's first policy ask looks before anything is published (decision
// 0239). Looking spends nothing and burns nothing, even when it does not
// match: only the spend that follows an approval may do either.
TEST_F(FieldValueRequestCoordinatorTest,
       LookingForAPreapprovalSpendsNothingAndBurnsNothing) {
  EXPECT_FALSE(coordinator_->HoldsFormFillPreapproval(
      kTaskId, kTabId, kNodeId, kRequestId, 0u, "https://bank.test", "frame-1",
      "page-1", 12u, 1u, 1u));
  OpenRequest();
  ASSERT_EQ(surface_mojom::FieldValueSupplyVerdict::kAccepted,
            Supply({"Alice"}).verdict);

  EXPECT_FALSE(coordinator_->HoldsFormFillPreapproval(
      kTaskId, kTabId, kNodeId, kRequestId, 0u, "https://other.test", "frame-1",
      "page-1", 12u, 1u, 1u));
  EXPECT_FALSE(coordinator_->HoldsFormFillPreapproval(
      kTaskId, kTabId, kNodeId, kRequestId, 1u, "https://bank.test", "frame-1",
      "page-1", 12u, 1u, 1u));
  EXPECT_TRUE(coordinator_->HoldsFormFillPreapproval(
      kTaskId, kTabId, kNodeId, kRequestId, 0u, "https://bank.test", "frame-1",
      "page-1", 12u, 1u, 1u));
  EXPECT_EQ(1u, coordinator_->preapproval_count_for_testing());

  EXPECT_TRUE(coordinator_->ConsumeFormFillPreapproval(
      kTaskId, kTabId, kNodeId, kRequestId, 0u, "https://bank.test", "frame-1",
      "page-1", 12u, 1u, 1u));
  EXPECT_FALSE(coordinator_->HoldsFormFillPreapproval(
      kTaskId, kTabId, kNodeId, kRequestId, 0u, "https://bank.test", "frame-1",
      "page-1", 12u, 1u, 1u));
}

TEST_F(FieldValueRequestCoordinatorTest,
       ChangedOriginBurnsTheExactPreapproval) {
  OpenRequest();
  ASSERT_EQ(surface_mojom::FieldValueSupplyVerdict::kAccepted,
            Supply({"Alice"}).verdict);
  EXPECT_FALSE(coordinator_->ConsumeFormFillPreapproval(
      kTaskId, kTabId, kNodeId, kRequestId, 0u, "https://other.test", "frame-1",
      "page-1", 12u, 1u, 1u));
  EXPECT_EQ(0u, coordinator_->preapproval_count_for_testing());
}

TEST_F(FieldValueRequestCoordinatorTest,
       AnOlderGraphRevisionBurnsThePreapproval) {
  OpenRequest();
  ASSERT_EQ(surface_mojom::FieldValueSupplyVerdict::kAccepted,
            Supply({"Alice"}).verdict);
  EXPECT_FALSE(coordinator_->ConsumeFormFillPreapproval(
      kTaskId, kTabId, kNodeId, kRequestId, 0u, "https://bank.test", "frame-1",
      "page-1", 11u, 1u, 1u));
  EXPECT_EQ(0u, coordinator_->preapproval_count_for_testing());
}

// The first fill changes the page it was made on: a script marks the field
// it typed into, and the next reading has a later revision. The second field
// is still the one the person approved, on the same document (decision 0194).
TEST_F(FieldValueRequestCoordinatorTest,
       ALaterReadingOfTheSameDocumentKeepsTheSequence) {
  ResolvedNodeFacts form;
  form.node_id = SemanticNodeId{kNodeId};
  form.observed_at_revision = 12u;
  form.form_field_node_ids = {SemanticNodeId{"field-a"},
                              SemanticNodeId{"field-b"}};
  node_facts_[kNodeId] = form;
  node_facts_["field-a"] = EditableField("field-a", Sensitivity::kIdentity);
  node_facts_["field-b"] = EditableField("field-b", Sensitivity::kPersonal);
  OpenRequest();
  ASSERT_EQ(surface_mojom::FieldValueSupplyVerdict::kAccepted,
            Supply({"1234 5678 9012", "AB12C"}).verdict);

  EXPECT_TRUE(coordinator_->ConsumeFormFillPreapproval(
      kTaskId, kTabId, "field-a", kRequestId, 0u, "https://bank.test",
      "frame-1", "page-1", 12u, 1u, 1u));
  EXPECT_TRUE(coordinator_->ConsumeFormFillPreapproval(
      kTaskId, kTabId, "field-b", kRequestId, 1u, "https://bank.test",
      "frame-1", "page-1", 15u, 1u, 1u));
  EXPECT_EQ(0u, coordinator_->preapproval_count_for_testing());
}

// A later revision is admitted only within the document the person answered
// on. The site moving the tab to another document still burns the sequence.
TEST_F(FieldValueRequestCoordinatorTest,
       AnotherDocumentBurnsTheSequenceWhateverItsRevision) {
  OpenRequest();
  ASSERT_EQ(surface_mojom::FieldValueSupplyVerdict::kAccepted,
            Supply({"Alice"}).verdict);
  EXPECT_FALSE(coordinator_->ConsumeFormFillPreapproval(
      kTaskId, kTabId, kNodeId, kRequestId, 0u, "https://bank.test", "frame-1",
      "page-2", 15u, 1u, 1u));
  EXPECT_EQ(0u, coordinator_->preapproval_count_for_testing());
}

TEST_F(FieldValueRequestCoordinatorTest, ExpiredPreapprovalCannotBeRevived) {
  OpenRequest();
  ASSERT_EQ(surface_mojom::FieldValueSupplyVerdict::kAccepted,
            Supply({"Alice"}).verdict);
  EXPECT_FALSE(coordinator_->ConsumeFormFillPreapproval(
      kTaskId, kTabId, kNodeId, kRequestId, 0u, "https://bank.test", "frame-1",
      "page-1", 12u, std::numeric_limits<uint64_t>::max(),
      std::numeric_limits<uint64_t>::max()));
  EXPECT_EQ(0u, coordinator_->preapproval_count_for_testing());
}

}  // namespace
}  // namespace taffy

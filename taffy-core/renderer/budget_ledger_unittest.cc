// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <limits>
#include <string>
#include <type_traits>

#include "base/test/scoped_mock_clock_override.h"
#include "taffy/renderer/adapters/adapter.h"
#include "taffy/renderer/adapters/bounded_web_text.h"
#include "taffy/renderer/observation_limits.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/blink/public/platform/web_string.h"

namespace taffy {
namespace {

using ExtractionContextArgumentsAcceptBorrowedFacts = std::is_constructible<
    ExtractionContext,
    blink::WebLocalFrame*,
    SemanticGraphStore*,
    BudgetLedger*,
    const ObservationLimits&,
    const ExtractedGraph*,
    const BrowserSuppliedFacts&,
    ExtractionScope>;
using ExtractionContextArgumentsRejectTemporaryFacts = std::is_constructible<
    ExtractionContext,
    blink::WebLocalFrame*,
    SemanticGraphStore*,
    BudgetLedger*,
    const ObservationLimits&,
    const ExtractedGraph*,
    BrowserSuppliedFacts&&,
    ExtractionScope>;
static_assert(ExtractionContextArgumentsAcceptBorrowedFacts::value);
static_assert(!ExtractionContextArgumentsRejectTemporaryFacts::value);

ObservationLimits LimitsWithTotalBytes(uint32_t max_total_bytes) {
  RequestedSnapshotBudget requested;
  requested.max_total_bytes = max_total_bytes;
  return ObservationLimits::NarrowedForTesting(requested);
}

TEST(BudgetLedgerTest, TextIsBoundedByTheTighterTotalByteBudget) {
  BudgetLedger ledger(LimitsWithTotalBytes(4).snapshot());

  EXPECT_EQ(ledger.BoundText("abcdef"), "abcd");
  EXPECT_TRUE(ledger.report().truncated);
  EXPECT_EQ(ledger.report().first_budget_reached, BudgetKind::kTotalBytes);
  EXPECT_EQ(ledger.report().omitted_text_bytes, 2u);
}

TEST(BudgetLedgerTest, TotalIncludesNodesEdgesAnnotationsAndWarnings) {
  SemanticNode node;
  node.node_id = SemanticNodeId("node-1");
  node.frame_id = FrameId("frame-1");
  node.name = "button";
  node.actions.push_back(ActionKind::kActivate);

  SemanticEdge edge;
  edge.from_frame_id = FrameId("frame-1");
  edge.from_node_id = SemanticNodeId("node-1");
  edge.to_frame_id = FrameId("frame-1");
  edge.to_node_id = SemanticNodeId("node-2");

  NodeAnnotation annotation;
  annotation.node_id = node.node_id;
  annotation.states.push_back(NodeState::kVisible);

  ObservationWarning warning(WarningCode::kAdapterUnavailable,
                             "bounded-warning");

  const size_t payload = BudgetLedger::ConservativeNodeBytes(node) +
                         BudgetLedger::ConservativeEdgeBytes(edge) +
                         BudgetLedger::ConservativeAnnotationBytes(annotation) +
                         BudgetLedger::ConservativeWarningBytes(warning);
  ASSERT_LE(payload, static_cast<size_t>(UINT32_MAX));
  BudgetLedger ledger(
      LimitsWithTotalBytes(static_cast<uint32_t>(payload)).snapshot());

  EXPECT_TRUE(ledger.ChargeNodePayload(node));
  EXPECT_TRUE(ledger.ChargeEdge(edge));
  EXPECT_TRUE(ledger.ChargeAnnotation(annotation));
  EXPECT_TRUE(ledger.ChargeWarning(warning));
  EXPECT_FALSE(ledger.ChargeBytes(1));
  EXPECT_EQ(ledger.report().first_budget_reached, BudgetKind::kTotalBytes);
}

// Decision 0166. The charge is the serialized form, never the resident one.
//
// This is the test that was missing when a phone's reading of an ordinary page
// stopped at 397 of about 658 nodes: `sizeof(SemanticNode)` is this process's
// cost for a struct whose empty optionals and empty vectors are dozens of bytes
// here and nothing at all on either hop out, so `snapshot_max_total_bytes`
// meant one quantity where it was enforced and a much larger one everywhere it
// was reasoned about. Nothing failed. The page was simply given to the model
// three fifths at a time.
TEST(BudgetLedgerTest, ANodeIsChargedItsFramingAndNotItsStructSize) {
  SemanticNode node;
  node.node_id = SemanticNodeId("node-1");
  node.frame_id = FrameId("frame-1");

  const SnapshotBudget& framing = ObservationLimits::ProcessSafeCeiling().snapshot();
  const size_t ids = std::string("node-1").size() + std::string("frame-1").size();
  EXPECT_EQ(BudgetLedger::ConservativeNodeBytes(node),
            framing.node_framing_bytes() + ids);

  // The point of the record, stated as an assertion rather than as prose: a
  // node with nothing on it costs less than the struct that holds it. If this
  // ever stops being true the charge has gone back to being resident cost.
  EXPECT_LT(BudgetLedger::ConservativeNodeBytes(node), sizeof(SemanticNode));
}

TEST(BudgetLedgerTest, AnEdgeIsChargedItsOwnFramingAndItsFourIdentifiers) {
  SemanticEdge edge;
  edge.from_frame_id = FrameId("frame-1");
  edge.from_node_id = SemanticNodeId("node-1");
  edge.to_frame_id = FrameId("frame-1");
  edge.to_node_id = SemanticNodeId("node-2");

  const SnapshotBudget& framing = ObservationLimits::ProcessSafeCeiling().snapshot();
  const size_t ids = std::string("frame-1").size() + std::string("node-1").size() +
                     std::string("frame-1").size() + std::string("node-2").size();
  EXPECT_EQ(BudgetLedger::ConservativeEdgeBytes(edge),
            framing.edge_framing_bytes() + ids);
  EXPECT_LT(BudgetLedger::ConservativeEdgeBytes(edge), sizeof(SemanticEdge));
}

TEST(BudgetLedgerTest, ATextRunCostsItsFramingAndItsText) {
  SemanticNode bare;
  bare.node_id = SemanticNodeId("node-1");
  bare.frame_id = FrameId("frame-1");

  SemanticNode with_text = bare;
  TextRun run;
  run.text = "Aadhaar Number";
  run.content_signals = 0;
  with_text.text_runs.push_back(run);

  const SnapshotBudget& framing = ObservationLimits::ProcessSafeCeiling().snapshot();
  EXPECT_EQ(BudgetLedger::ConservativeNodeBytes(with_text) -
                BudgetLedger::ConservativeNodeBytes(bare),
            framing.text_run_framing_bytes() + std::string("Aadhaar Number").size());
}

TEST(BudgetLedgerTest, AClosedVocabularyMemberCostsOneListElement) {
  SemanticNode bare;
  bare.node_id = SemanticNodeId("node-1");
  bare.frame_id = FrameId("frame-1");

  SemanticNode acting = bare;
  acting.actions.push_back(ActionKind::kActivate);

  const SnapshotBudget& framing = ObservationLimits::ProcessSafeCeiling().snapshot();
  EXPECT_EQ(BudgetLedger::ConservativeNodeBytes(acting) -
                BudgetLedger::ConservativeNodeBytes(bare),
            framing.list_element_bytes());
}

// The framing terms are estimate terms, not bounds. A caller may ask for fewer
// nodes or fewer bytes; what one node costs to encode is a property of the
// encoding and not something a request may change.
TEST(BudgetLedgerTest, NarrowingLeavesTheFramingTermsAlone) {
  const SnapshotBudget& ceiling = ObservationLimits::ProcessSafeCeiling().snapshot();
  const SnapshotBudget narrowed = LimitsWithTotalBytes(64).snapshot();

  EXPECT_LT(narrowed.max_total_bytes(), ceiling.max_total_bytes());
  EXPECT_EQ(narrowed.node_framing_bytes(), ceiling.node_framing_bytes());
  EXPECT_EQ(narrowed.edge_framing_bytes(), ceiling.edge_framing_bytes());
  EXPECT_EQ(narrowed.text_run_framing_bytes(), ceiling.text_run_framing_bytes());
  EXPECT_EQ(narrowed.list_element_bytes(), ceiling.list_element_bytes());
}

TEST(BudgetLedgerTest, DeadlineCheckpointRecordsTheBudgetItReached) {
  base::ScopedMockClockOverride clock;
  RequestedSnapshotBudget requested;
  requested.deadline_ms = 5;
  const ObservationLimits limits =
      ObservationLimits::NarrowedForTesting(requested);
  BudgetLedger ledger(limits.snapshot());

  EXPECT_TRUE(ledger.CheckDeadline());
  clock.Advance(base::Milliseconds(5));
  EXPECT_FALSE(ledger.CheckDeadline());
  EXPECT_EQ(ledger.report().first_budget_reached, BudgetKind::kDeadline);
}

TEST(BudgetLedgerTest, OversizedChargeCannotWrapTheCounter) {
  BudgetLedger ledger(LimitsWithTotalBytes(16).snapshot());
  EXPECT_FALSE(ledger.ChargeBytes(std::numeric_limits<size_t>::max()));
  EXPECT_FALSE(ledger.ChargeBytes(17));
  EXPECT_EQ(ledger.report().first_budget_reached, BudgetKind::kTotalBytes);
}

TEST(BudgetLedgerTest, RemainingNodesTracksClaimsWithoutUnderflow) {
  RequestedSnapshotBudget requested;
  requested.max_nodes = 2;
  const ObservationLimits limits =
      ObservationLimits::NarrowedForTesting(requested);
  BudgetLedger ledger(limits.snapshot());

  EXPECT_EQ(ledger.RemainingNodes(), 2u);
  EXPECT_TRUE(ledger.ChargeNode());
  EXPECT_EQ(ledger.RemainingNodes(), 1u);
  EXPECT_TRUE(ledger.ChargeNode());
  EXPECT_EQ(ledger.RemainingNodes(), 0u);
  EXPECT_FALSE(ledger.ChargeNode());
  EXPECT_EQ(ledger.RemainingNodes(), 0u);
}

TEST(BudgetLedgerTest, WebStringConversionStopsAtTheRemainingByteBudget) {
  RequestedSnapshotBudget requested;
  requested.max_text_bytes = 4;
  const ObservationLimits limits =
      ObservationLimits::NarrowedForTesting(requested);
  BudgetLedger ledger(limits.snapshot());

  const BoundedWebText bounded =
      BoundWebText(blink::WebString::FromUtf8(std::string(100, 'a')), ledger,
                   limits.snapshot().max_text_bytes());

  EXPECT_EQ(bounded.text, "aaaa");
  EXPECT_TRUE(bounded.truncated);
  EXPECT_EQ(ledger.report().omitted_text_bytes, 96u);
  EXPECT_TRUE(ledger.report().omitted_data_could_change_answer);
}

TEST(BudgetLedgerTest, ExhaustedTextBudgetSkipsTheBlinkRead) {
  RequestedSnapshotBudget requested;
  requested.max_text_bytes = 1;
  const ObservationLimits limits =
      ObservationLimits::NarrowedForTesting(requested);
  BudgetLedger ledger(limits.snapshot());
  ASSERT_EQ(ledger.BoundText("x"), "x");

  int reads = 0;
  const BoundedWebText bounded = BoundWebTextLazily(
      [&reads]() {
        ++reads;
        return blink::WebString::FromUtf8(std::string(1024, 'a'));
      },
      ledger, limits.snapshot().max_text_bytes());

  EXPECT_EQ(reads, 0);
  EXPECT_TRUE(bounded.text.empty());
  EXPECT_TRUE(bounded.truncated);
  EXPECT_TRUE(ledger.report().truncated);
  EXPECT_TRUE(ledger.report().omitted_data_could_change_answer);
}

TEST(BudgetLedgerTest, LazyWebStringReadRunsOnceWhenBudgetRemains) {
  RequestedSnapshotBudget requested;
  requested.max_text_bytes = 4;
  const ObservationLimits limits =
      ObservationLimits::NarrowedForTesting(requested);
  BudgetLedger ledger(limits.snapshot());

  int reads = 0;
  const BoundedWebText bounded = BoundWebTextLazily(
      [&reads]() {
        ++reads;
        return blink::WebString::FromUtf8("abcdef");
      },
      ledger, limits.snapshot().max_text_bytes());

  EXPECT_EQ(reads, 1);
  EXPECT_EQ(bounded.text, "abcd");
  EXPECT_TRUE(bounded.truncated);
}

}  // namespace
}  // namespace taffy

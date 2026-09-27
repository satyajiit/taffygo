// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/renderer/snapshot_graph_projection.h"

#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "taffy/renderer/observation_limits.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

SemanticNode Node(std::string id,
                  SemanticRole role,
                  std::vector<NodeState> states = {}) {
  SemanticNode node;
  node.node_id = SemanticNodeId(std::move(id));
  node.frame_id = FrameId("frame-main");
  node.role = role;
  node.states = std::move(states);
  return node;
}

SemanticEdge Contains(std::string from, std::string to) {
  SemanticEdge edge;
  edge.from_frame_id = FrameId("frame-main");
  edge.from_node_id = SemanticNodeId(std::move(from));
  edge.to_frame_id = FrameId("frame-main");
  edge.to_node_id = SemanticNodeId(std::move(to));
  edge.relationship = EdgeType::kContains;
  return edge;
}

SemanticEdge SameEntityAs(std::string from, std::string to) {
  SemanticEdge edge;
  edge.from_frame_id = FrameId("frame-main");
  edge.from_node_id = SemanticNodeId(std::move(from));
  edge.to_frame_id = FrameId("frame-main");
  edge.to_node_id = SemanticNodeId(std::move(to));
  edge.relationship = EdgeType::kSameEntityAs;
  return edge;
}

std::set<std::string> ProjectedNodeIds(const mojom::PageSnapshot& snapshot) {
  std::set<std::string> ids;
  for (const mojom::SemanticNodePtr& node : snapshot.nodes) {
    if (node) {
      ids.insert(node->node_id);
    }
  }
  return ids;
}

TEST(SnapshotGraphProjectionTest, SectionKeepsOnlyNamedFormSubtree) {
  ExtractedGraph graph;
  graph.nodes.push_back(Node("document", SemanticRole::kDocument));
  graph.nodes.push_back(Node("form-a", SemanticRole::kRegion));
  graph.nodes.push_back(Node("field-a", SemanticRole::kTextField));
  graph.nodes.push_back(Node("form-b", SemanticRole::kRegion));
  graph.nodes.push_back(Node("field-b", SemanticRole::kTextField));
  graph.edges.push_back(Contains("document", "form-a"));
  graph.edges.push_back(Contains("form-a", "field-a"));
  graph.edges.push_back(Contains("document", "form-b"));
  graph.edges.push_back(Contains("form-b", "field-b"));

  BudgetLedger ledger(ObservationLimits::ProcessSafeCeiling().snapshot());
  mojom::PageSnapshot snapshot;
  std::vector<mojom::SnapshotWarningPtr> warnings;
  const SnapshotProjectionSummary summary = ProjectSnapshotGraph(
      ExtractionScope::kSection, SemanticNodeId("form-a"),
      /*media_root=*/std::nullopt,
      /*selection_node_ids=*/{}, graph,
      /*dom_stopped_at_shadow_boundary=*/false, &ledger, &snapshot, &warnings);

  EXPECT_FALSE(summary.incomplete);
  EXPECT_EQ(ProjectedNodeIds(snapshot),
            (std::set<std::string>{"field-a", "form-a"}));
  ASSERT_EQ(snapshot.edges.size(), 1u);
  EXPECT_EQ(snapshot.edges[0]->from_node_id, "form-a");
  EXPECT_EQ(snapshot.edges[0]->to_node_id, "field-a");
}

TEST(SnapshotGraphProjectionTest,
     SectionKeepsExactIdentityWithoutAccessibilityContainmentBroadening) {
  ExtractedGraph graph;
  graph.nodes.push_back(Node("form", SemanticRole::kRegion));
  graph.nodes.push_back(Node("dom-control", SemanticRole::kTextField));
  graph.nodes.push_back(Node("ax-control", SemanticRole::kTextField));
  graph.nodes.push_back(Node("ax-group", SemanticRole::kRegion));
  graph.nodes.push_back(Node("ax-sibling", SemanticRole::kTextField));
  graph.edges.push_back(Contains("form", "dom-control"));
  graph.edges.push_back(SameEntityAs("ax-control", "dom-control"));
  // These generic AX containment edges are deliberately plausible but are
  // not form membership. Following either after the identity join would
  // widen the exact SECTION to a visual group and its unrelated sibling.
  graph.edges.push_back(Contains("ax-group", "ax-control"));
  graph.edges.push_back(Contains("ax-group", "ax-sibling"));

  BudgetLedger ledger(ObservationLimits::ProcessSafeCeiling().snapshot());
  mojom::PageSnapshot snapshot;
  std::vector<mojom::SnapshotWarningPtr> warnings;
  const SnapshotProjectionSummary summary = ProjectSnapshotGraph(
      ExtractionScope::kSection, SemanticNodeId("form"),
      /*media_root=*/std::nullopt,
      /*selection_node_ids=*/{}, graph,
      /*dom_stopped_at_shadow_boundary=*/false, &ledger, &snapshot, &warnings);

  EXPECT_FALSE(summary.incomplete);
  EXPECT_EQ(ProjectedNodeIds(snapshot),
            (std::set<std::string>{"ax-control", "dom-control", "form"}));
  ASSERT_EQ(snapshot.edges.size(), 2u);
  EXPECT_EQ(snapshot.edges[0]->relationship,
            mojom::RelationshipKind::kContains);
  EXPECT_EQ(snapshot.edges[1]->relationship,
            mojom::RelationshipKind::kSameEntityAs);
}

TEST(SnapshotGraphProjectionTest, SelectionKeepsOnlyExplicitlySelectedNodes) {
  ExtractedGraph graph;
  graph.nodes.push_back(Node("document", SemanticRole::kDocument));
  graph.nodes.push_back(
      Node("selection", SemanticRole::kRegion, {NodeState::kSelected}));
  graph.nodes.push_back(
      Node("selected-text", SemanticRole::kParagraph, {NodeState::kSelected}));
  // Accessibility uses the same state for controls selected in page UI. It
  // must not be mistaken for membership in the live document selection.
  graph.nodes.push_back(Node("selected-control-outside-range",
                             SemanticRole::kOption, {NodeState::kSelected}));
  graph.nodes.push_back(Node("unrelated-region", SemanticRole::kRegion));
  graph.nodes.push_back(Node("unselected-text", SemanticRole::kParagraph));
  graph.edges.push_back(Contains("selection", "selected-text"));
  graph.edges.push_back(Contains("unrelated-region", "unselected-text"));

  BudgetLedger ledger(ObservationLimits::ProcessSafeCeiling().snapshot());
  mojom::PageSnapshot snapshot;
  std::vector<mojom::SnapshotWarningPtr> warnings;
  const SnapshotProjectionSummary summary = ProjectSnapshotGraph(
      ExtractionScope::kSelection, std::nullopt,
      /*media_root=*/std::nullopt,
      {SemanticNodeId("selection"), SemanticNodeId("selected-text")}, graph,
      /*dom_stopped_at_shadow_boundary=*/false, &ledger, &snapshot, &warnings);

  EXPECT_FALSE(summary.incomplete);
  EXPECT_EQ(ProjectedNodeIds(snapshot),
            (std::set<std::string>{"selected-text", "selection"}));
  ASSERT_EQ(snapshot.edges.size(), 1u);
  EXPECT_EQ(snapshot.edges[0]->from_node_id, "selection");
  EXPECT_EQ(snapshot.edges[0]->to_node_id, "selected-text");
}

TEST(SnapshotGraphProjectionTest, MediaRootKeepsOnlyExactNamedNode) {
  ExtractedGraph graph;
  graph.nodes.push_back(Node("document", SemanticRole::kDocument));
  graph.nodes.push_back(Node("image", SemanticRole::kImage));
  graph.nodes.push_back(Node("caption", SemanticRole::kParagraph));
  graph.nodes.push_back(Node("unrelated", SemanticRole::kParagraph));
  graph.edges.push_back(Contains("document", "image"));
  graph.edges.push_back(Contains("image", "caption"));

  BudgetLedger ledger(ObservationLimits::ProcessSafeCeiling().snapshot());
  mojom::PageSnapshot snapshot;
  std::vector<mojom::SnapshotWarningPtr> warnings;
  const SnapshotProjectionSummary summary = ProjectSnapshotGraph(
      ExtractionScope::kDocument, /*section_root=*/std::nullopt,
      SemanticNodeId("image"), /*selection_node_ids=*/{}, graph,
      /*dom_stopped_at_shadow_boundary=*/false, &ledger, &snapshot, &warnings);

  EXPECT_FALSE(summary.incomplete);
  EXPECT_EQ(ProjectedNodeIds(snapshot), (std::set<std::string>{"image"}));
  EXPECT_TRUE(snapshot.edges.empty());
}

}  // namespace
}  // namespace taffy

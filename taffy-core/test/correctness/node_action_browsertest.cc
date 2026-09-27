// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

#include "taffy/components/intelligence/content/bip_graph_payload.h"
#include "taffy/contracts/bip/mojom/page_intelligence.mojom.h"
#include "taffy/common/public/bip_observation.h"
#include "taffy/test/support/bip_graph_payload_reader.h"
#include "taffy/test/support/scripted_bip_client.h"
#include "taffy/test/support/bip_request_builder.h"
#include "taffy/test/support/taffy_observation_test_base.h"
#include "content/public/test/browser_test.h"
#include "testing/gtest/include/gtest/gtest.h"

// What an observation of a real document actually contains, and what stays
// still when the document does.
//
// Every other suite in this directory reads the envelope's scalars — counts,
// codes, revisions — and none of them opens the graph. That left two things
// unproven at once: that the graph payload carries node identity a caller
// could act on, and that a page which never changes produces a graph that
// never changes. The second turned out to be false, and the first is what made
// it findable.

namespace taffy::test {
namespace {

// SemanticRole::kLink, as the framing carries it. Written as the enumerator
// rather than as a number so that inserting a role above it is a compile-time
// question rather than a silently different node.
constexpr uint16_t kLinkRole = static_cast<uint16_t>(mojom::SemanticRole::kLink);

class NodeActionTest : public TaffyObservationTestBase {
 protected:
  std::vector<GraphPayloadNode> NodesOf(const ObservationEnvelope& envelope) {
    const std::optional<std::vector<GraphPayloadNode>> nodes =
        ReadGraphPayloadNodes(envelope.graph_payload);
    EXPECT_TRUE(nodes.has_value())
        << "The graph payload did not decode. The browser's encoder and this "
           "reader are the two independent halves of one framing, and a "
           "failure here means they have drifted.";
    return nodes.value_or(std::vector<GraphPayloadNode>());
  }

  ObservationEnvelope ObserveAgain() {
    return client().Observe(
        builder().Observation(MainFrameId(), ObservationScope::kDocument));
  }
};

// The graph carries identity, not only counts. Without this nothing in the
// tree establishes that a caller could ever name a node the renderer produced
// — which is why every action suite here targets a fabricated identifier and
// asserts a refusal.
IN_PROC_BROWSER_TEST_F(NodeActionTest, ALinkNodeIsNamedInTheGraphOfARealPage) {
  const ObservationEnvelope envelope = ObserveFixture("static-article");
  ASSERT_EQ(GraphPayloadEncoding::kBipContract, envelope.encoding)
      << "The observation carried no graph. The shipping browser installs a "
         "BIP encoder (taffy_page_intelligence_host.cc); a test base that "
         "does not is testing a browser nobody ships.";

  const std::vector<GraphPayloadNode> nodes = NodesOf(envelope);
  ASSERT_FALSE(nodes.empty());
  EXPECT_EQ(envelope.node_count, nodes.size())
      << "The envelope's count and the graph disagree about how many nodes "
         "crossed.";

  const auto link = std::ranges::find_if(
      nodes, [](const GraphPayloadNode& node) { return node.role == kLinkRole; });
  ASSERT_NE(link, nodes.end())
      << "No link node in the graph of a fixture with two anchors in it.";
  EXPECT_FALSE(link->node_id.empty());
  EXPECT_FALSE(link->frame_id.empty());
  EXPECT_EQ(envelope.root_frame_id.value, link->frame_id)
      << "The graph row leaked the renderer-local frame identifier instead "
         "of the browser-owned identity that can resolve the node.";
}

// The regression test for a defect this file found.
//
// Observing a document with no script in it three times moved its graph
// revision from 97 to 289 to 481 on a physical device: every describing
// adapter re-upserted every node without the bounds and states the layout
// adapter had annotated, each re-upsert read as a change, and the annotation
// that followed read as another. Two revisions per node, per observation,
// forever.
//
// The consequence was not noise. SemanticGraphStore::Resolve refuses a node
// whose `last_changed` is above the revision the caller authorized against, so
// with the revision climbing on its own, no node-targeted action could ever be
// authorized. Nothing caught it because every action test in the tree asserts
// a refusal, and a dispatcher that refuses everything satisfies all of them.
IN_PROC_BROWSER_TEST_F(NodeActionTest,
                       ObservingAStaticPageTwiceDoesNotMoveItsRevision) {
  const ObservationEnvelope first = ObserveFixture("static-article");
  const ObservationEnvelope second = ObserveAgain();
  const ObservationEnvelope third = ObserveAgain();

  EXPECT_EQ(first.page_epoch, second.page_epoch);
  EXPECT_EQ(first.graph_revision, second.graph_revision)
      << "The graph revision moved between two observations of a document "
         "that did not change. Every node-targeted action authorized against "
         "the first observation is now refused as stale.";
  EXPECT_EQ(first.graph_revision, third.graph_revision);

  // And the node an action would target keeps its identity across them.
  //
  // Only the actionable node is checked, deliberately. Some rows in this graph
  // do get a new identifier on a second observation of the same document —
  // `n10` became `n105` here — and that is a separate subject from the
  // revision: identity is keyed on a DOM node, and a row synthesised without
  // one has nothing to be keyed on. It is worth knowing and it is not what
  // this test is about. What an action needs is that the node it names is
  // still that node, and a link is keyed on its anchor.
  const auto link_of = [this](const ObservationEnvelope& envelope) {
    const std::vector<GraphPayloadNode> nodes = NodesOf(envelope);
    const auto found = std::ranges::find_if(
        nodes,
        [](const GraphPayloadNode& node) { return node.role == kLinkRole; });
    return found == nodes.end() ? std::string() : found->node_id;
  };
  const std::string before = link_of(first);
  ASSERT_FALSE(before.empty());
  EXPECT_EQ(before, link_of(third))
      << "The first link node has a different identifier after two more "
         "observations of a document that did not change. A handle taken from "
         "one observation would name nothing in the next.";
}


// `Destination::is_cross_origin` is the one bit of a link the model is told to
// read, and until 2026-09-14 the accessibility adapter never set it.
//
// That is worth a test of its own because of what depends on it and where it
// broke. The errand's compiled-in instruction names this word — a result that
// says "another site" after it is the node carrying the address — and on a
// search results page the accessibility adapter spends the shared node budget
// before the DOM adapter runs, so every result a model was shown claimed to
// stay on the engine. Nothing failed: the field has a `false` default, both
// adapters produce link nodes, and a suite asserting "some link is
// cross-origin" would have passed on the DOM adapter's rows alone.
//
// So the assertion is tied to a named node rather than to a count. Whichever
// adapter emitted the row for a given anchor, that row's flag has to be right.
IN_PROC_BROWSER_TEST_F(NodeActionTest, ALinksDestinationSaysWhetherItLeavesTheSite) {
  const ObservationEnvelope envelope = ObserveFixture("search-results");
  const std::vector<GraphPayloadNode> nodes = NodesOf(envelope);
  ASSERT_FALSE(nodes.empty());

  constexpr uint8_t kPresent =
      static_cast<uint8_t>(BipDestinationFlag::kPresent);
  constexpr uint8_t kCrossOrigin =
      static_cast<uint8_t>(BipDestinationFlag::kCrossOrigin);

  // The two results this fixture names, one on each side of the boundary. The
  // partner row is the only anchor in the corpus page that leaves the origin.
  constexpr char kLeavesTheSite[] =
      "Seller listing C — Lumen Arc (partner origin)";
  constexpr char kStaysOnTheSite[] =
      "Lumen Arc desk lamp — manufacturer page";

  size_t checked_leaving = 0;
  size_t checked_staying = 0;
  for (const GraphPayloadNode& node : nodes) {
    if (node.role != kLinkRole || node.name_withheld) {
      continue;
    }
    const bool leaves = node.name == kLeavesTheSite;
    if (!leaves && node.name != kStaysOnTheSite) {
      continue;
    }
    (leaves ? checked_leaving : checked_staying)++;
    EXPECT_EQ(kPresent, node.destination_flags & kPresent)
        << "A link node carried no destination at all: " << node.name;
    EXPECT_EQ(leaves ? kCrossOrigin : 0,
              node.destination_flags & kCrossOrigin)
        << "The cross-origin bit is wrong for \"" << node.name
        << "\". A model choosing a result that leaves the engine reads this "
           "word and nothing else; flags=" << int{node.destination_flags};
  }

  // Both have to have been reached, or the loop above asserted nothing. A
  // withheld name or a renamed fixture row lands here rather than passing.
  EXPECT_GE(checked_leaving, 1u)
      << "No link node named the partner result. Either the corpus page "
         "changed or every link's name was withheld, and in both cases the "
         "assertion above ran zero times.";
  EXPECT_GE(checked_staying, 1u)
      << "No link node named the same-origin result.";
}

}  // namespace
}  // namespace taffy::test

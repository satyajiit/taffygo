// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/renderer/node_precondition_checker.h"

#include <optional>
#include <utility>

#include "taffy/renderer/semantic_graph_store.h"
#include "testing/gtest/include/gtest/gtest.h"

// The renderer-local half of protocol section 12. Every test here is about
// the same property said a different way: a check that cannot be performed is
// a refusal, and there is no path through Check() that produces kOk by
// omission.

namespace taffy {
namespace {

using IdentitySpace = SemanticGraphStore::IdentitySpace;

DocumentFacts ActiveDocument() {
  DocumentFacts facts;
  facts.has_document = true;
  facts.origin_is_opaque = false;
  facts.origin_serialization = "https://primary.taffy.test";
  return facts;
}

SemanticGraphStore::LiveNode MakeLink(SemanticGraphStore& store,
                                      SemanticNodeId id,
                                      int64_t dom_node_id) {
  SemanticGraphStore::LiveNode node;
  node.node_id = id;
  node.dom_key = store.MakeKey(IdentitySpace::kDom, dom_node_id);
  node.role = SemanticRole::kLink;
  node.actions = {ActionKind::kActivate, ActionKind::kScrollIntoView};
  node.sensitivity = Sensitivity::kNotSensitive;
  node.states = {NodeState::kVisible, NodeState::kEnabled};
  Destination destination;
  destination.url = "https://primary.taffy.test/product/lumen-desk-lamp.html";
  node.destination = destination;
  node.occlusion_determined = true;
  return node;
}

PreconditionRequest ActivateRequest(SemanticNodeId id) {
  PreconditionRequest request;
  request.node_id = id;
  request.expected_page_epoch = PageEpoch("epoch_a1");
  request.requested_action = ActionKind::kActivate;
  request.expected_role = SemanticRole::kLink;
  request.required_states = {NodeState::kVisible, NodeState::kEnabled};
  request.max_sensitivity = Sensitivity::kNotSensitive;
  return request;
}

TEST(NodePreconditionCheckerTest, AcceptsAnUnchangedNode) {
  SemanticGraphStore store(FrameId("frame_main"), PageEpoch("epoch_a1"));
  const SemanticNodeId id =
      store.AllocateOrLookup(store.MakeKey(IdentitySpace::kDom, 7));
  store.UpsertLiveNode(MakeLink(store, id, 7));

  const SemanticGraphStore::ScopedActionBarrier barrier(&store);
  const NodePreconditionChecker checker;
  const PreconditionResult result = checker.Check(
      store, barrier, ActiveDocument(), ActivateRequest(id));
  EXPECT_EQ(result.code, PreconditionCode::kOk);
  ASSERT_TRUE(result.node.has_value());
  EXPECT_EQ(result.node->node_id, id);
}

TEST(NodePreconditionCheckerTest, NoDocumentIsARefusal) {
  SemanticGraphStore store(FrameId("frame_main"), PageEpoch("epoch_a1"));
  const SemanticNodeId id =
      store.AllocateOrLookup(store.MakeKey(IdentitySpace::kDom, 7));
  store.UpsertLiveNode(MakeLink(store, id, 7));

  const SemanticGraphStore::ScopedActionBarrier barrier(&store);
  const NodePreconditionChecker checker;
  EXPECT_EQ(checker.Check(store, barrier, DocumentFacts(), ActivateRequest(id))
                .code,
            PreconditionCode::kUnsupported);
}

TEST(NodePreconditionCheckerTest, OpaqueOriginNeverSatisfiesAnOriginCheck) {
  // Every opaque origin serializes to the same token. Comparing strings would
  // report a match between two unrelated opaque documents, so the check
  // refuses instead and lets the browser's nonce comparison decide.
  SemanticGraphStore store(FrameId("frame_main"), PageEpoch("epoch_a1"));
  const SemanticNodeId id =
      store.AllocateOrLookup(store.MakeKey(IdentitySpace::kDom, 7));
  store.UpsertLiveNode(MakeLink(store, id, 7));

  DocumentFacts opaque;
  opaque.has_document = true;
  opaque.origin_is_opaque = true;

  PreconditionRequest request = ActivateRequest(id);
  request.expected_origin_serialization = "https://primary.taffy.test";

  const SemanticGraphStore::ScopedActionBarrier barrier(&store);
  const NodePreconditionChecker checker;
  EXPECT_EQ(checker.Check(store, barrier, opaque, request).code,
            PreconditionCode::kOriginChanged);
}

TEST(NodePreconditionCheckerTest, RetiredNodeIsGoneNotStale) {
  SemanticGraphStore store(FrameId("frame_main"), PageEpoch("epoch_a1"));
  const SemanticNodeId id =
      store.AllocateOrLookup(store.MakeKey(IdentitySpace::kDom, 7));
  store.UpsertLiveNode(MakeLink(store, id, 7));
  store.Retire(id);

  const SemanticGraphStore::ScopedActionBarrier barrier(&store);
  const NodePreconditionChecker checker;
  EXPECT_EQ(checker.Check(store, barrier, ActiveDocument(),
                          ActivateRequest(id))
                .code,
            PreconditionCode::kNodeGone);
}

TEST(NodePreconditionCheckerTest, SwappedDestinationIsRefused) {
  // The single most valuable attack against an assistant that follows links:
  // change the href after the plan was made. "Close enough" has no meaning
  // here, so the comparison is literal.
  SemanticGraphStore store(FrameId("frame_main"), PageEpoch("epoch_a1"));
  const SemanticNodeId id =
      store.AllocateOrLookup(store.MakeKey(IdentitySpace::kDom, 7));
  store.UpsertLiveNode(MakeLink(store, id, 7));

  PreconditionRequest request = ActivateRequest(id);
  request.expected_destination = "https://hostile.taffy.test/collect";

  const SemanticGraphStore::ScopedActionBarrier barrier(&store);
  const NodePreconditionChecker checker;
  EXPECT_EQ(checker.Check(store, barrier, ActiveDocument(), request).code,
            PreconditionCode::kDestinationChanged);
}

TEST(NodePreconditionCheckerTest, SensitiveNodeIsRefused) {
  SemanticGraphStore store(FrameId("frame_main"), PageEpoch("epoch_a1"));
  const SemanticNodeId id =
      store.AllocateOrLookup(store.MakeKey(IdentitySpace::kDom, 7));
  SemanticGraphStore::LiveNode node = MakeLink(store, id, 7);
  node.sensitivity = Sensitivity::kCredential;
  store.UpsertLiveNode(node);

  const SemanticGraphStore::ScopedActionBarrier barrier(&store);
  const NodePreconditionChecker checker;
  EXPECT_EQ(checker.Check(store, barrier, ActiveDocument(),
                          ActivateRequest(id))
                .code,
            PreconditionCode::kSensitiveField);
}

TEST(NodePreconditionCheckerTest, ContentTrustChangeIsARefusal) {
  SemanticGraphStore store(FrameId("frame_main"), PageEpoch("epoch_a1"));
  const SemanticNodeId id =
      store.AllocateOrLookup(store.MakeKey(IdentitySpace::kDom, 7));
  SemanticGraphStore::LiveNode node = MakeLink(store, id, 7);
  node.content_trust = RendererContentTrust::kThirdPartyEmbedded;
  store.UpsertLiveNode(node);

  PreconditionRequest request = ActivateRequest(id);
  request.content_trust_check_declared = true;
  request.forbidden_content_trust =
      RendererContentTrust::kThirdPartyEmbedded;
  const SemanticGraphStore::ScopedActionBarrier barrier(&store);
  const NodePreconditionChecker checker;
  EXPECT_EQ(checker.Check(store, barrier, ActiveDocument(), request).code,
            PreconditionCode::kUnsupported);
}

TEST(NodePreconditionCheckerTest, UnknownContentTrustNeverPassesAGuard) {
  SemanticGraphStore store(FrameId("frame_main"), PageEpoch("epoch_a1"));
  const SemanticNodeId id =
      store.AllocateOrLookup(store.MakeKey(IdentitySpace::kDom, 7));
  SemanticGraphStore::LiveNode node = MakeLink(store, id, 7);
  node.content_trust = RendererContentTrust::kUnknownUntrusted;
  store.UpsertLiveNode(node);

  PreconditionRequest request = ActivateRequest(id);
  request.content_trust_check_declared = true;
  request.forbidden_content_trust = RendererContentTrust::kUserGeneratedContent;
  const SemanticGraphStore::ScopedActionBarrier barrier(&store);
  const NodePreconditionChecker checker;
  EXPECT_EQ(checker.Check(store, barrier, ActiveDocument(), request).code,
            PreconditionCode::kUnsupported);
}

TEST(NodePreconditionCheckerTest, DifferentKnownAuthorshipPassesTheGuard) {
  SemanticGraphStore store(FrameId("frame_main"), PageEpoch("epoch_a1"));
  const SemanticNodeId id =
      store.AllocateOrLookup(store.MakeKey(IdentitySpace::kDom, 7));
  SemanticGraphStore::LiveNode node = MakeLink(store, id, 7);
  node.content_trust = RendererContentTrust::kFirstPartyDocument;
  store.UpsertLiveNode(node);

  PreconditionRequest request = ActivateRequest(id);
  request.content_trust_check_declared = true;
  request.forbidden_content_trust =
      RendererContentTrust::kThirdPartyEmbedded;
  const SemanticGraphStore::ScopedActionBarrier barrier(&store);
  const NodePreconditionChecker checker;
  EXPECT_EQ(checker.Check(store, barrier, ActiveDocument(), request).code,
            PreconditionCode::kOk);
}

TEST(NodePreconditionCheckerTest, RequiredStateMustBeAsserted) {
  // Silence is "could not tell", not "true". A node that never asserted
  // kVisible does not satisfy a requirement for it.
  SemanticGraphStore store(FrameId("frame_main"), PageEpoch("epoch_a1"));
  const SemanticNodeId id =
      store.AllocateOrLookup(store.MakeKey(IdentitySpace::kDom, 7));
  SemanticGraphStore::LiveNode node = MakeLink(store, id, 7);
  node.states = {NodeState::kEnabled};
  store.UpsertLiveNode(node);

  const SemanticGraphStore::ScopedActionBarrier barrier(&store);
  const NodePreconditionChecker checker;
  EXPECT_EQ(checker.Check(store, barrier, ActiveDocument(),
                          ActivateRequest(id))
                .code,
            PreconditionCode::kNotVisible);
}

// Where the node is now outranks where the reading left it, in both
// directions, and a measurement that could not be taken changes nothing. A
// field read below the fold and scrolled into view since was refused as not
// visible on a phone (decision 0250).
TEST(NodePreconditionCheckerTest, VisibilityIsJudgedWhereTheNodeIsNow) {
  SemanticGraphStore store(FrameId("frame_main"), PageEpoch("epoch_a1"));
  const SemanticNodeId read_below_the_fold =
      store.AllocateOrLookup(store.MakeKey(IdentitySpace::kDom, 7));
  SemanticGraphStore::LiveNode below = MakeLink(store, read_below_the_fold, 7);
  below.states = {NodeState::kOffscreen, NodeState::kNotVisible,
                  NodeState::kEnabled};
  below.occlusion_determined = false;
  store.UpsertLiveNode(below);
  const SemanticNodeId read_in_view =
      store.AllocateOrLookup(store.MakeKey(IdentitySpace::kDom, 8));
  store.UpsertLiveNode(MakeLink(store, read_in_view, 8));

  MeasuredVisibility in_view;
  in_view.states = {NodeState::kVisible};
  in_view.occlusion_determined = true;
  MeasuredVisibility off_screen;
  off_screen.states = {NodeState::kOffscreen, NodeState::kNotVisible};

  const NodePreconditionChecker checker;
  const auto check = [&](SemanticNodeId id,
                         std::optional<MeasuredVisibility> now) {
    DocumentFacts facts = ActiveDocument();
    facts.visibility_now = std::move(now);
    PreconditionRequest request = ActivateRequest(id);
    request.forbidden_states = {NodeState::kObscured, NodeState::kOffscreen};
    const SemanticGraphStore::ScopedActionBarrier barrier(&store);
    return checker.Check(store, barrier, facts, request).code;
  };
  EXPECT_EQ(check(read_below_the_fold, std::nullopt),
            PreconditionCode::kNotVisible);
  EXPECT_EQ(check(read_below_the_fold, in_view), PreconditionCode::kOk);
  EXPECT_EQ(check(read_in_view, off_screen), PreconditionCode::kNotVisible);
  EXPECT_EQ(check(read_in_view, std::nullopt), PreconditionCode::kOk);
}

TEST(NodePreconditionCheckerTest, UndeterminedOcclusionRefusesTheAction) {
  // The regression this test exists for. A forbidden-state check that only
  // looked for an ASSERTED kObscured would pass for every node whose
  // occlusion was never probed - which is every node once the probe budget
  // runs out, and every node on a document with no accessibility tree. An
  // action policy would then read "we did not look" as "not obscured".
  SemanticGraphStore store(FrameId("frame_main"), PageEpoch("epoch_a1"));
  const SemanticNodeId id =
      store.AllocateOrLookup(store.MakeKey(IdentitySpace::kDom, 7));
  SemanticGraphStore::LiveNode node = MakeLink(store, id, 7);
  node.occlusion_determined = false;
  store.UpsertLiveNode(node);

  PreconditionRequest request = ActivateRequest(id);
  request.forbidden_states = {NodeState::kObscured};

  const SemanticGraphStore::ScopedActionBarrier barrier(&store);
  const NodePreconditionChecker checker;
  EXPECT_EQ(checker.Check(store, barrier, ActiveDocument(), request).code,
            PreconditionCode::kUnsupported);
}

TEST(NodePreconditionCheckerTest, DeterminedOcclusionIsHonoured) {
  SemanticGraphStore store(FrameId("frame_main"), PageEpoch("epoch_a1"));
  const SemanticNodeId visible_id =
      store.AllocateOrLookup(store.MakeKey(IdentitySpace::kDom, 7));
  store.UpsertLiveNode(MakeLink(store, visible_id, 7));

  const SemanticNodeId covered_id =
      store.AllocateOrLookup(store.MakeKey(IdentitySpace::kDom, 8));
  SemanticGraphStore::LiveNode covered = MakeLink(store, covered_id, 8);
  covered.states.push_back(NodeState::kObscured);
  store.UpsertLiveNode(covered);

  const NodePreconditionChecker checker;
  {
    PreconditionRequest request = ActivateRequest(visible_id);
    request.forbidden_states = {NodeState::kObscured};
    const SemanticGraphStore::ScopedActionBarrier barrier(&store);
    EXPECT_EQ(checker.Check(store, barrier, ActiveDocument(), request).code,
              PreconditionCode::kOk);
  }
  {
    PreconditionRequest request = ActivateRequest(covered_id);
    request.forbidden_states = {NodeState::kObscured};
    const SemanticGraphStore::ScopedActionBarrier barrier(&store);
    EXPECT_EQ(checker.Check(store, barrier, ActiveDocument(), request).code,
              PreconditionCode::kOccluded);
  }
}

TEST(NodePreconditionCheckerTest, MovedTargetIsRefused) {
  // Bounds are compared, never resolved from. A consent dialog that slid a
  // different button under the point a plan was made about changes nothing
  // else about the node.
  SemanticGraphStore store(FrameId("frame_main"), PageEpoch("epoch_a1"));
  const SemanticNodeId id =
      store.AllocateOrLookup(store.MakeKey(IdentitySpace::kDom, 7));
  SemanticGraphStore::LiveNode node = MakeLink(store, id, 7);
  NodeBounds bounds;
  bounds.x = 10;
  bounds.y = 20;
  bounds.width = 100;
  bounds.height = 40;
  node.bounds = bounds;
  store.UpsertLiveNode(node);

  PreconditionRequest request = ActivateRequest(id);
  request.expected_bounds = bounds;

  const NodePreconditionChecker checker;
  {
    const SemanticGraphStore::ScopedActionBarrier barrier(&store);
    EXPECT_EQ(checker.Check(store, barrier, ActiveDocument(), request).code,
              PreconditionCode::kOk);
  }

  NodeBounds moved = bounds;
  moved.y = 220;
  node.bounds = moved;
  store.UpsertLiveNode(node);
  {
    const SemanticGraphStore::ScopedActionBarrier barrier(&store);
    // The node record moved, so the revision moved with it; the request
    // states no minimum revision, so this is purely the bounds comparison.
    EXPECT_EQ(checker.Check(store, barrier, ActiveDocument(), request).code,
              PreconditionCode::kNotVisible);
  }
}

TEST(NodePreconditionCheckerTest, GraphMovingDuringPreflightIsRefused) {
  SemanticGraphStore store(FrameId("frame_main"), PageEpoch("epoch_a1"));
  const SemanticNodeId id =
      store.AllocateOrLookup(store.MakeKey(IdentitySpace::kDom, 7));
  store.UpsertLiveNode(MakeLink(store, id, 7));

  const SemanticGraphStore::ScopedActionBarrier barrier(&store);
  store.NoteChange(SemanticGraphStore::ChangeClass::kRouteTransition);

  const NodePreconditionChecker checker;
  EXPECT_EQ(checker.Check(store, barrier, ActiveDocument(),
                          ActivateRequest(id))
                .code,
            PreconditionCode::kGraphMovedDuringPreflight);
}

TEST(NodePreconditionCheckerTest, WrongEpochIsRefusedBeforeAnythingElse) {
  SemanticGraphStore store(FrameId("frame_main"), PageEpoch("epoch_a1"));
  const SemanticNodeId id =
      store.AllocateOrLookup(store.MakeKey(IdentitySpace::kDom, 7));
  store.UpsertLiveNode(MakeLink(store, id, 7));

  PreconditionRequest request = ActivateRequest(id);
  request.expected_page_epoch = PageEpoch("epoch_a2");

  const SemanticGraphStore::ScopedActionBarrier barrier(&store);
  const NodePreconditionChecker checker;
  EXPECT_EQ(checker.Check(store, barrier, ActiveDocument(), request).code,
            PreconditionCode::kStalePageEpoch);
}

TEST(NodePreconditionCheckerTest, MissingActionIsRefused) {
  SemanticGraphStore store(FrameId("frame_main"), PageEpoch("epoch_a1"));
  const SemanticNodeId id =
      store.AllocateOrLookup(store.MakeKey(IdentitySpace::kDom, 7));
  SemanticGraphStore::LiveNode node = MakeLink(store, id, 7);
  node.actions = {ActionKind::kScrollIntoView};
  store.UpsertLiveNode(node);

  const SemanticGraphStore::ScopedActionBarrier barrier(&store);
  const NodePreconditionChecker checker;
  EXPECT_EQ(checker.Check(store, barrier, ActiveDocument(),
                          ActivateRequest(id))
                .code,
            PreconditionCode::kRoleOrActionChanged);
}

}  // namespace
}  // namespace taffy

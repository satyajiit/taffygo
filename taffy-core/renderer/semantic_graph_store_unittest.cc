// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/renderer/semantic_graph_store.h"

#include <algorithm>
#include <optional>
#include <set>

#include "testing/gtest/include/gtest/gtest.h"

// These tests are the reason SemanticGraphStore holds no Blink object: the
// identity invariants of protocol sections 5.3, 5.4 and 8.3 are provable on any
// host, and the properties they prove are the ones a security review asks
// about. They run in the `fast` lane, not only on the Chromium track.

namespace taffy {
namespace {

using IdentitySpace = SemanticGraphStore::IdentitySpace;

SemanticGraphStore MakeStore() {
  return SemanticGraphStore(FrameId("frame_main"), PageEpoch("epoch_a1"));
}

// The least a producing adapter has to say about a node before the store will
// resolve it: the id it was allocated under, the key it was allocated from,
// and a role. Written once because it is a contract, and a contract copied
// into every test is a contract with several places to drift.
SemanticGraphStore::LiveNode Described(const SemanticGraphStore& store,
                                       const SemanticNodeId& id,
                                       int64_t source,
                                       SemanticRole role) {
  SemanticGraphStore::LiveNode node;
  node.node_id = id;
  node.dom_key = store.MakeKey(IdentitySpace::kDom, source);
  node.role = role;
  return node;
}

TEST(SemanticGraphStoreTest, AllocatesStableIdForSameKey) {
  SemanticGraphStore store = MakeStore();
  const SemanticNodeId first =
      store.AllocateOrLookup(store.MakeKey(IdentitySpace::kDom, 1));
  const SemanticNodeId second =
      store.AllocateOrLookup(store.MakeKey(IdentitySpace::kDom, 1));
  EXPECT_EQ(first, second);
}

TEST(SemanticGraphStoreTest, IdentitySpacesDoNotCollide) {
  // A DOM node id and an accessibility object id are unrelated sequences of
  // small integers. Keying on the number alone would merge them.
  SemanticGraphStore store = MakeStore();
  const SemanticNodeId dom =
      store.AllocateOrLookup(store.MakeKey(IdentitySpace::kDom, 3));
  const SemanticNodeId ax =
      store.AllocateOrLookup(store.MakeKey(IdentitySpace::kAccessibility, 3));
  EXPECT_NE(dom, ax);
}

TEST(SemanticGraphStoreTest, RetiredIdIsNeverIssuedAgain) {
  SemanticGraphStore store = MakeStore();
  std::set<SemanticNodeId> ever_issued;

  for (int round = 0; round < 100; ++round) {
    // The same DOM node, removed and re-observed over and over. This is the
    // virtualized-list shape: the element persists, the logical row does not.
    const SemanticNodeId id =
        store.AllocateOrLookup(store.MakeKey(IdentitySpace::kDom, 1));
    EXPECT_TRUE(ever_issued.insert(id).second)
        << "id " << id.value() << " was issued twice in round " << round;
    store.RetireByDomNode(IdentitySpace::kDom, 1);
    EXPECT_TRUE(store.IsRetired(id));
  }
  EXPECT_EQ(ever_issued.size(), 100u);
}

TEST(SemanticGraphStoreTest, ResolveDistinguishesGoneFromUnknown) {
  SemanticGraphStore store = MakeStore();
  const SemanticNodeId id =
      store.AllocateOrLookup(store.MakeKey(IdentitySpace::kDom, 5));
  store.UpsertLiveNode(Described(store, id, 5, SemanticRole::kLink));

  EXPECT_EQ(store.Resolve(id, PageEpoch("epoch_a1"), std::nullopt).status,
            SemanticGraphStore::ResolveStatus::kOk);

  store.Retire(id);
  EXPECT_EQ(store.Resolve(id, PageEpoch("epoch_a1"), std::nullopt).status,
            SemanticGraphStore::ResolveStatus::kNodeGone);

  EXPECT_EQ(store
                .Resolve(SemanticNodeId("n-never-issued"),
                         PageEpoch("epoch_a1"), std::nullopt)
                .status,
            SemanticGraphStore::ResolveStatus::kNodeUnknown);
}

TEST(SemanticGraphStoreTest, WrongEpochIsRefusedBeforeAnythingElse) {
  // A handle from another document must not even reach the node lookup: it is
  // a handle for a page that no longer exists, and that is a different
  // failure from a stale node.
  SemanticGraphStore store = MakeStore();
  const SemanticNodeId id =
      store.AllocateOrLookup(store.MakeKey(IdentitySpace::kDom, 5));
  EXPECT_EQ(store.Resolve(id, PageEpoch("epoch_a2"), std::nullopt).status,
            SemanticGraphStore::ResolveStatus::kStalePageEpoch);
}

TEST(SemanticGraphStoreTest, StaleRevisionIsRefused) {
  SemanticGraphStore store = MakeStore();
  const SemanticNodeId id =
      store.AllocateOrLookup(store.MakeKey(IdentitySpace::kDom, 5));
  SemanticGraphStore::LiveNode node =
      Described(store, id, 5, SemanticRole::kLink);
  store.UpsertLiveNode(node);

  const GraphRevision observed = store.current_revision();

  // The graph moving is not by itself staleness: the caller's decision was
  // about this node, and this node has not changed.
  store.NoteChange(SemanticGraphStore::ChangeClass::kRouteTransition);
  EXPECT_EQ(store.Resolve(id, PageEpoch("epoch_a1"), observed).status,
            SemanticGraphStore::ResolveStatus::kOk);

  // This node changing is.
  node.role = SemanticRole::kButton;
  store.UpsertLiveNode(node);
  EXPECT_EQ(store.Resolve(id, PageEpoch("epoch_a1"), observed).status,
            SemanticGraphStore::ResolveStatus::kStaleGraph);
  EXPECT_EQ(
      store.Resolve(id, PageEpoch("epoch_a1"), store.current_revision()).status,
      SemanticGraphStore::ResolveStatus::kOk);
}

TEST(SemanticGraphStoreTest, PreconditionRelevantChangeAdvancesRevision) {
  SemanticGraphStore store = MakeStore();
  const SemanticNodeId id =
      store.AllocateOrLookup(store.MakeKey(IdentitySpace::kDom, 5));
  SemanticGraphStore::LiveNode node =
      Described(store, id, 5, SemanticRole::kLink);
  Destination destination;
  destination.url = "https://example.test/a";
  node.destination = destination;
  store.UpsertLiveNode(node);

  const GraphRevision before = store.current_revision();

  // Re-observing the same facts must not advance the revision: a revision
  // that moves for cosmetic reasons trains callers to re-request constantly.
  store.UpsertLiveNode(node);
  EXPECT_EQ(store.current_revision(), before);

  // A swapped destination must advance it. This is the case that matters:
  // a page that changes an href after a plan was made.
  destination.url = "https://example.test/b";
  node.destination = destination;
  store.UpsertLiveNode(node);
  EXPECT_NE(store.current_revision(), before);
}

TEST(SemanticGraphStoreTest, ValueChangeEvidenceIsExactNodeAndFormStateOnly) {
  SemanticGraphStore store = MakeStore();
  const SemanticNodeId id =
      store.AllocateOrLookup(store.MakeKey(IdentitySpace::kDom, 5));
  SemanticGraphStore::LiveNode node =
      Described(store, id, 5, SemanticRole::kTextField);
  store.UpsertLiveNode(node);

  store.NoteNodeChange(SemanticGraphStore::ChangeClass::kAccessibleStateChanged,
                       id);
  auto resolved = store.Resolve(id, PageEpoch("epoch_a1"), std::nullopt);
  ASSERT_TRUE(resolved.node.has_value());
  EXPECT_EQ(resolved.node->value_changed_at_revision, GraphRevision(0));

  store.NoteNodeChange(SemanticGraphStore::ChangeClass::kFormStateChanged,
                       SemanticNodeId("unknown"));
  resolved = store.Resolve(id, PageEpoch("epoch_a1"), std::nullopt);
  ASSERT_TRUE(resolved.node.has_value());
  EXPECT_EQ(resolved.node->value_changed_at_revision, GraphRevision(0));

  const GraphRevision before_value_change = store.current_revision();
  store.NoteNodeChange(SemanticGraphStore::ChangeClass::kFormStateChanged, id);
  resolved = store.Resolve(id, PageEpoch("epoch_a1"), std::nullopt);
  ASSERT_TRUE(resolved.node.has_value());
  EXPECT_GT(resolved.node->value_changed_at_revision, before_value_change);

  // A later adapter description must preserve event evidence that it did not
  // originate and cannot reconstruct from the value (which it may not read).
  store.UpsertLiveNode(node);
  resolved = store.Resolve(id, PageEpoch("epoch_a1"), std::nullopt);
  ASSERT_TRUE(resolved.node.has_value());
  EXPECT_GT(resolved.node->value_changed_at_revision, before_value_change);
}

// The surviving half of the alternating-describers defect: a describer that
// is ignorant of a field (an absent destination, the "could not tell"
// sensitivity and role defaults, an empty action list) must not be read as
// the field changing. Before the carry-forward covered these four, a pair of
// alternating describers moved `last_changed` on every observation of a
// static page, and Resolve refused every node-targeted action as
// kStaleGraph.
TEST(SemanticGraphStoreTest, IgnorantRedescriptionIsNotAChange) {
  SemanticGraphStore store = MakeStore();
  const SemanticNodeId id =
      store.AllocateOrLookup(store.MakeKey(IdentitySpace::kDom, 5));

  // The knowing describer: role, actions, destination, sensitivity.
  SemanticGraphStore::LiveNode knowing =
      Described(store, id, 5, SemanticRole::kLink);
  knowing.actions = {ActionKind::kActivate, ActionKind::kScrollIntoView};
  knowing.sensitivity = Sensitivity::kNotSensitive;
  Destination destination;
  destination.url = "https://example.test/a";
  knowing.destination = destination;
  store.UpsertLiveNode(knowing);

  const GraphRevision observed = store.current_revision();

  // The ignorant describer: same node, defaults everywhere the first one
  // carried knowledge. Alternate the two several times.
  SemanticGraphStore::LiveNode ignorant =
      Described(store, id, 5, SemanticRole::kUnknownContent);
  for (int round = 0; round < 3; ++round) {
    store.UpsertLiveNode(ignorant);
    store.UpsertLiveNode(knowing);
  }
  EXPECT_EQ(store.current_revision(), observed);
  EXPECT_EQ(store.Resolve(id, PageEpoch("epoch_a1"), observed).status,
            SemanticGraphStore::ResolveStatus::kOk);

  // Ignorance carried nothing away: the stored node still knows everything
  // the knowing describer said.
  const SemanticGraphStore::ResolveResult resolved =
      store.Resolve(id, PageEpoch("epoch_a1"), observed);
  ASSERT_TRUE(resolved.node.has_value());
  EXPECT_EQ(resolved.node->role, SemanticRole::kLink);
  EXPECT_EQ(resolved.node->sensitivity, Sensitivity::kNotSensitive);
  ASSERT_TRUE(resolved.node->destination.has_value());
  EXPECT_EQ(resolved.node->destination->url, "https://example.test/a");
  EXPECT_EQ(resolved.node->actions.size(), 2u);

  // A describer that KNOWS a different value is still a change.
  knowing.sensitivity = Sensitivity::kPersonal;
  store.UpsertLiveNode(knowing);
  EXPECT_EQ(store.Resolve(id, PageEpoch("epoch_a1"), observed).status,
            SemanticGraphStore::ResolveStatus::kStaleGraph);
}

TEST(SemanticGraphStoreTest,
     AuthoritativeFormRedescriptionCanClearAnOldClearance) {
  SemanticGraphStore store = MakeStore();
  const SemanticNodeId id =
      store.AllocateOrLookup(store.MakeKey(IdentitySpace::kDom, 9));

  SemanticGraphStore::LiveNode form =
      Described(store, id, 9, SemanticRole::kTextField);
  form.actions = {ActionKind::kSetText, ActionKind::kScrollIntoView};
  form.sensitivity = Sensitivity::kIdentity;
  form.challenge_kind = ChallengeKind::kOneTimeCode;
  form.challenge_dom_node_id = 91;
  form.states = {NodeState::kEnabled, NodeState::kEditable,
                 NodeState::kRequired, NodeState::kVisible};
  form.form_semantics_authoritative = true;
  store.UpsertLiveNode(form);
  const GraphRevision observed = store.current_revision();

  // A generic DOM description has no form classification. Even if its
  // defaults look permissive, it cannot weaken the exact form facts or create
  // revision churn before the form adapter runs again.
  SemanticGraphStore::LiveNode generic =
      Described(store, id, 9, SemanticRole::kUnknownContent);
  generic.actions = {ActionKind::kScrollIntoView};
  generic.sensitivity = Sensitivity::kNotSensitive;
  store.UpsertLiveNode(std::move(generic));
  EXPECT_EQ(store.current_revision(), observed);

  // The next exact form read is allowed to say the previous classification
  // no longer applies. Unknown-sensitive is a real fail-safe answer, not the
  // absence of an answer, and empty actions really means no writable action.
  form.actions.clear();
  form.sensitivity = Sensitivity::kUnknownSensitive;
  form.challenge_kind = ChallengeKind::kNone;
  form.challenge_dom_node_id.reset();
  form.states = {NodeState::kDisabled, NodeState::kReadOnly};
  store.UpsertLiveNode(std::move(form));

  const SemanticGraphStore::ResolveResult resolved =
      store.Resolve(id, PageEpoch("epoch_a1"), std::nullopt);
  ASSERT_TRUE(resolved.node.has_value());
  EXPECT_EQ(resolved.node->sensitivity, Sensitivity::kUnknownSensitive);
  EXPECT_EQ(resolved.node->challenge_kind, ChallengeKind::kNone);
  EXPECT_FALSE(resolved.node->challenge_dom_node_id.has_value());
  EXPECT_TRUE(resolved.node->actions.empty());
  EXPECT_NE(std::find(resolved.node->states.begin(), resolved.node->states.end(),
                      NodeState::kDisabled),
            resolved.node->states.end());
  EXPECT_NE(std::find(resolved.node->states.begin(), resolved.node->states.end(),
                      NodeState::kReadOnly),
            resolved.node->states.end());
  EXPECT_EQ(std::find(resolved.node->states.begin(), resolved.node->states.end(),
                      NodeState::kEditable),
            resolved.node->states.end());
  EXPECT_EQ(std::find(resolved.node->states.begin(), resolved.node->states.end(),
                      NodeState::kRequired),
            resolved.node->states.end());
  // Visibility belongs to the layout path and survives the form-only update.
  EXPECT_NE(std::find(resolved.node->states.begin(), resolved.node->states.end(),
                      NodeState::kVisible),
            resolved.node->states.end());
  EXPECT_EQ(store.Resolve(id, PageEpoch("epoch_a1"), observed).status,
            SemanticGraphStore::ResolveStatus::kStaleGraph);
}

TEST(SemanticGraphStoreTest,
     ChallengePresentationTargetTracksOnlyLiveAuthoritativeFacts) {
  SemanticGraphStore store = MakeStore();
  const SemanticNodeId id =
      store.AllocateOrLookup(store.MakeKey(IdentitySpace::kDom, 9));

  SemanticGraphStore::LiveNode field =
      Described(store, id, 9, SemanticRole::kTextField);
  field.form_semantics_authoritative = true;
  field.challenge_kind = ChallengeKind::kImage;
  field.challenge_dom_node_id = 91;
  store.UpsertLiveNode(field);

  EXPECT_TRUE(
      store.IsChallengePresentationTarget(91, ChallengeKind::kImage));
  EXPECT_FALSE(
      store.IsChallengePresentationTarget(91, ChallengeKind::kInteractive));
  EXPECT_FALSE(
      store.IsChallengePresentationTarget(92, ChallengeKind::kImage));

  field.challenge_kind = ChallengeKind::kNone;
  field.challenge_dom_node_id.reset();
  store.UpsertLiveNode(field);
  EXPECT_FALSE(
      store.IsChallengePresentationTarget(91, ChallengeKind::kImage));
}

TEST(SemanticGraphStoreTest, AuthoritativeFormCanClearItsExactChildList) {
  SemanticGraphStore store = MakeStore();
  const SemanticNodeId form_id =
      store.AllocateOrLookup(store.MakeKey(IdentitySpace::kDom, 12));
  const SemanticNodeId field_id =
      store.AllocateOrLookup(store.MakeKey(IdentitySpace::kDom, 13));
  SemanticGraphStore::LiveNode form =
      Described(store, form_id, 12, SemanticRole::kRegion);
  form.form_semantics_authoritative = true;
  form.form_field_node_ids = std::vector<SemanticNodeId>{field_id};
  store.UpsertLiveNode(form);
  const GraphRevision observed = store.current_revision();

  form.form_field_node_ids = std::vector<SemanticNodeId>();
  store.UpsertLiveNode(std::move(form));
  const SemanticGraphStore::ResolveResult resolved =
      store.Resolve(form_id, PageEpoch("epoch_a1"), std::nullopt);
  ASSERT_TRUE(resolved.node.has_value());
  ASSERT_TRUE(resolved.node->form_field_node_ids.has_value());
  EXPECT_TRUE(resolved.node->form_field_node_ids->empty());
  EXPECT_EQ(store.Resolve(form_id, PageEpoch("epoch_a1"), observed).status,
            SemanticGraphStore::ResolveStatus::kStaleGraph);
}

// Two describers may know the same actions in different orders. The set is
// what an action checks, so a reordering is not a change.
TEST(SemanticGraphStoreTest, ActionOrderIsNotAChange) {
  SemanticGraphStore store = MakeStore();
  const SemanticNodeId id =
      store.AllocateOrLookup(store.MakeKey(IdentitySpace::kDom, 5));
  SemanticGraphStore::LiveNode node =
      Described(store, id, 5, SemanticRole::kLink);
  node.actions = {ActionKind::kActivate, ActionKind::kScrollIntoView};
  store.UpsertLiveNode(node);

  const GraphRevision before = store.current_revision();
  node.actions = {ActionKind::kScrollIntoView, ActionKind::kActivate,
                  ActionKind::kActivate};
  store.UpsertLiveNode(node);
  EXPECT_EQ(store.current_revision(), before);
}

TEST(SemanticGraphStoreTest, ActionBarrierSeesGraphMove) {
  SemanticGraphStore store = MakeStore();
  {
    const SemanticGraphStore::ScopedActionBarrier barrier(&store);
    EXPECT_TRUE(store.action_barrier_active());
    EXPECT_FALSE(barrier.graph_moved());
    store.NoteChange(SemanticGraphStore::ChangeClass::kAccessibleStateChanged);
    // Spec section 5.3: a change during a preflight is exactly what must not
    // be coalesced away, so the barrier reports it.
    EXPECT_TRUE(barrier.graph_moved());
  }
  EXPECT_FALSE(store.action_barrier_active());
}

TEST(SemanticGraphStoreTest, NestedBarriersUnwindCorrectly) {
  SemanticGraphStore store = MakeStore();
  {
    const SemanticGraphStore::ScopedActionBarrier outer(&store);
    {
      const SemanticGraphStore::ScopedActionBarrier inner(&store);
      EXPECT_TRUE(store.action_barrier_active());
    }
    EXPECT_TRUE(store.action_barrier_active());
  }
  EXPECT_FALSE(store.action_barrier_active());
}

TEST(SemanticGraphStoreTest, AnnotationsMergeWithoutReplacing) {
  SemanticGraphStore store = MakeStore();
  const SemanticNodeId id =
      store.AllocateOrLookup(store.MakeKey(IdentitySpace::kDom, 5));
  SemanticGraphStore::LiveNode node =
      Described(store, id, 5, SemanticRole::kLink);
  node.states = {NodeState::kEnabled};
  store.UpsertLiveNode(node);

  NodeBounds bounds;
  bounds.x = 4;
  bounds.y = 8;
  bounds.width = 40;
  bounds.height = 12;
  EXPECT_TRUE(store.AnnotateLiveNode(id, {NodeState::kVisible}, bounds, true));

  const SemanticGraphStore::LiveNode* live = store.FindLive(id);
  ASSERT_TRUE(live);
  // The producing adapter's state survives the annotator's.
  EXPECT_EQ(live->states.size(), 2u);
  EXPECT_TRUE(live->occlusion_determined);
  ASSERT_TRUE(live->bounds.has_value());
  EXPECT_EQ(live->bounds->height, 12);

  // One-way: an adapter that did not probe cannot un-probe one that did.
  EXPECT_TRUE(store.AnnotateLiveNode(id, {}, std::nullopt, false));
  EXPECT_TRUE(store.FindLive(id)->occlusion_determined);
}

TEST(SemanticGraphStoreTest, AnnotatingAnUnknownNodeFails) {
  // An annotation must never mint an identity. A node with states and no
  // description is exactly the shape a stale handle has.
  SemanticGraphStore store = MakeStore();
  EXPECT_FALSE(store.AnnotateLiveNode(SemanticNodeId("n-never-issued"),
                                      {NodeState::kVisible}, std::nullopt,
                                      true));
}

TEST(SemanticGraphStoreTest, LookupNeverAllocates) {
  SemanticGraphStore store = MakeStore();
  const auto key = store.MakeKey(IdentitySpace::kDom, 11);
  EXPECT_FALSE(store.Lookup(key).has_value());
  const SemanticNodeId id = store.AllocateOrLookup(key);
  EXPECT_EQ(store.Lookup(store.MakeKey(IdentitySpace::kDom, 11)), id);

  store.Retire(id);
  // Retirement erases the reverse mapping, so the DOM node cannot lead back
  // to the retired id by any path - including this one.
  EXPECT_FALSE(
      store.Lookup(store.MakeKey(IdentitySpace::kDom, 11)).has_value());
}

}  // namespace
}  // namespace taffy

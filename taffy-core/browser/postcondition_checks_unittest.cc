// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/components/intelligence/content/postcondition_checks.h"

#include <vector>

#include "testing/gtest/include/gtest/gtest.h"

// The DISPATCHED-versus-VERIFIED boundary, one postcondition class at a time
// (protocol section 11.6).
//
// Nothing in this file constructs a renderer acknowledgement, because there is
// nothing in the evidence types to construct it with. That is the boundary,
// and it is the reason these tests can only fail by being wrong about a page,
// never by being wrong about who to believe.

namespace taffy {
namespace {

Origin Tuple(const char* serialization) {
  Origin origin;
  origin.kind = OriginKind::kTuple;
  origin.serialization = serialization;
  return origin;
}

ObservationEvidence Observed(GraphRevision dispatch_revision,
                             GraphRevision observed_revision,
                             std::vector<NodeState> states) {
  ObservationEvidence evidence;
  evidence.attempted = true;
  evidence.node_resolved = true;
  evidence.handle_still_live = true;
  evidence.dispatch_revision = dispatch_revision;
  evidence.observed_at_revision = observed_revision;
  ResolvedNodeFacts facts;
  facts.observed_at_revision = observed_revision;
  facts.asserted_states = std::move(states);
  evidence.facts = facts;
  return evidence;
}

// --- verifiability ----------------------------------------------------------

TEST(PostconditionChecksTest, AnUnqualifiedStateChangeCarriesNoClaim) {
  Postcondition postcondition;
  postcondition.kind = PostconditionKind::kNodeStateChanged;
  // "Something about this node is different" would be satisfied by a revision
  // bump for any reason at all, which is not a claim about the action.
  EXPECT_FALSE(PostconditionCarriesAnObservableClaim(postcondition));

  postcondition.expected_node_state = NodeState::kChecked;
  EXPECT_TRUE(PostconditionCarriesAnObservableClaim(postcondition));
}

TEST(PostconditionChecksTest, ValueChangeNeedsNoValueDerivedMaterial) {
  Postcondition postcondition;
  postcondition.kind = PostconditionKind::kNodeValueChanged;
  EXPECT_TRUE(PostconditionCarriesAnObservableClaim(postcondition));

  postcondition.expected_value_digest =
      ContentDigest{DigestAlgorithm::kSha256, "forbidden"};
  EXPECT_FALSE(PostconditionCarriesAnObservableClaim(postcondition));
}

TEST(PostconditionChecksTest, AnUnqualifiedNavigationCarriesNoClaim) {
  Postcondition postcondition;
  postcondition.kind = PostconditionKind::kCommittedNavigation;
  EXPECT_FALSE(PostconditionCarriesAnObservableClaim(postcondition));

  postcondition.allowed_origins.push_back(Tuple("https://primary.taffy.test"));
  EXPECT_TRUE(PostconditionCarriesAnObservableClaim(postcondition));
}

TEST(PostconditionChecksTest, LoadingStopRequiresTheBrowserLifecycleEvent) {
  Postcondition postcondition;
  postcondition.kind = PostconditionKind::kLoadingStopped;
  EXPECT_TRUE(PostconditionCarriesAnObservableClaim(postcondition));

  NavigationEvidence evidence;
  EXPECT_EQ(PostconditionCheck::kPending,
            CheckLoadingStopped(postcondition, evidence));
  evidence.loading_stopped = true;
  EXPECT_EQ(PostconditionCheck::kSatisfied,
            CheckLoadingStopped(postcondition, evidence));
}

TEST(PostconditionChecksTest, AnEmptyListIsNotVerifiable) {
  EXPECT_FALSE(AllPostconditionsAreVerifiable({}));
}

TEST(PostconditionChecksTest, OneUnverifiableEntrySpoilsTheList) {
  Postcondition good;
  good.kind = PostconditionKind::kNoMutation;
  Postcondition bad;
  bad.kind = PostconditionKind::kBrowserFlowStarted;
  EXPECT_FALSE(AllPostconditionsAreVerifiable({good, bad}));

  bad.expected_flow = BrowserFlowKind::kDownload;
  EXPECT_TRUE(AllPostconditionsAreVerifiable({good, bad}));
}

// --- no mutation ------------------------------------------------------------

TEST(PostconditionChecksTest, NoMutationIsSatisfiedWhileNothingMoved) {
  Postcondition postcondition;
  postcondition.kind = PostconditionKind::kNoMutation;
  ObservationEvidence evidence = Observed(5, 5, {});
  EXPECT_EQ(CheckNoMutation(postcondition, evidence),
            PostconditionCheck::kSatisfied);
}

TEST(PostconditionChecksTest, NoMutationIsContradictedByARevisionBump) {
  Postcondition postcondition;
  postcondition.kind = PostconditionKind::kNoMutation;
  EXPECT_EQ(CheckNoMutation(postcondition, Observed(5, 6, {})),
            PostconditionCheck::kContradicted);
}

TEST(PostconditionChecksTest, NoMutationIsContradictedByADeadHandle) {
  Postcondition postcondition;
  postcondition.kind = PostconditionKind::kNoMutation;
  ObservationEvidence evidence = Observed(5, 5, {});
  evidence.handle_still_live = false;
  EXPECT_EQ(CheckNoMutation(postcondition, evidence),
            PostconditionCheck::kContradicted);
}

// --- committed navigation ---------------------------------------------------

TEST(PostconditionChecksTest, ATaskTabMustNotReachItsOpener) {
  Postcondition postcondition;
  postcondition.kind = PostconditionKind::kNewTabCreated;
  postcondition.allowed_origins.push_back(Tuple("https://partner.taffy.test"));
  postcondition.expects_opener_reference = false;

  TabEvidence evidence;
  evidence.tab_created = true;
  evidence.destination_origin = Tuple("https://partner.taffy.test");
  evidence.has_opener_reference = true;
  EXPECT_EQ(CheckNewTabCreated(postcondition, evidence),
            PostconditionCheck::kContradicted);

  evidence.has_opener_reference = false;
  EXPECT_EQ(CheckNewTabCreated(postcondition, evidence),
            PostconditionCheck::kSatisfied);
}

// --- observed classes -------------------------------------------------------

TEST(PostconditionChecksTest, StateBeforeDispatchIsNotTheActionsEffect) {
  Postcondition postcondition;
  postcondition.kind = PostconditionKind::kNodeStateChanged;
  postcondition.expected_node_state = NodeState::kChecked;

  // Already checked, but observed at the dispatch revision: this is state the
  // action did not cause.
  EXPECT_EQ(CheckNodeStateChanged(postcondition,
                                  Observed(9, 9, {NodeState::kChecked})),
            PostconditionCheck::kPending);
  EXPECT_EQ(CheckNodeStateChanged(postcondition,
                                  Observed(9, 10, {NodeState::kChecked})),
            PostconditionCheck::kSatisfied);
}

TEST(PostconditionChecksTest, ANodeThatWentAwayContradictsAStateChange) {
  Postcondition postcondition;
  postcondition.kind = PostconditionKind::kNodeStateChanged;
  postcondition.expected_node_state = NodeState::kChecked;

  ObservationEvidence evidence;
  evidence.attempted = true;
  evidence.node_gone = true;
  evidence.dispatch_revision = 9;
  EXPECT_EQ(CheckNodeStateChanged(postcondition, evidence),
            PostconditionCheck::kContradicted);
}

TEST(PostconditionChecksTest, SectionVisibleRefusesAnObscuredNode) {
  Postcondition postcondition;
  postcondition.kind = PostconditionKind::kSectionVisible;

  EXPECT_EQ(CheckSectionVisible(
                postcondition,
                Observed(1, 2, {NodeState::kVisible, NodeState::kObscured})),
            PostconditionCheck::kPending);
  EXPECT_EQ(
      CheckSectionVisible(postcondition, Observed(1, 2, {NodeState::kVisible})),
      PostconditionCheck::kSatisfied);
  EXPECT_EQ(CheckSectionVisible(postcondition,
                                Observed(1, 2, {NodeState::kNotVisible})),
            PostconditionCheck::kContradicted);
}

// A scroll into view of a line already in view moves nothing, so no reading
// newer than its dispatch ever comes. Its claim, that the line is in view, is
// true all the same (decision 0245). Only that direction is read at the
// dispatch revision: "off screen" there may be the reading from before the
// scroll.
TEST(PostconditionChecksTest, SectionVisibleIsSatisfiedByALineAlreadyInView) {
  Postcondition postcondition;
  postcondition.kind = PostconditionKind::kSectionVisible;

  EXPECT_EQ(
      CheckSectionVisible(postcondition, Observed(2, 2, {NodeState::kVisible})),
      PostconditionCheck::kSatisfied);
  EXPECT_EQ(CheckSectionVisible(postcondition,
                                Observed(2, 2, {NodeState::kOffscreen})),
            PostconditionCheck::kPending);
  // A reading older than the dispatch is about another page, even a visible
  // one.
  EXPECT_EQ(
      CheckSectionVisible(postcondition, Observed(2, 1, {NodeState::kVisible})),
      PostconditionCheck::kPending);
  EXPECT_EQ(CheckSectionVisible(
                postcondition,
                Observed(2, 2, {NodeState::kVisible, NodeState::kObscured})),
            PostconditionCheck::kPending);
}

TEST(PostconditionChecksTest, SearchNeedsBothTheCommitAndTheResults) {
  Postcondition postcondition;
  postcondition.kind = PostconditionKind::kSearchResultState;
  postcondition.allowed_origins.push_back(Tuple("https://primary.taffy.test"));
  postcondition.expected_node_state = NodeState::kVisible;

  NavigationEvidence navigation;
  ObservationEvidence observation = Observed(1, 2, {NodeState::kVisible});

  // Results observed but no browser-owned commit: search is a browser command
  // precisely so that somebody other than the page confirms it ran.
  EXPECT_EQ(CheckSearchResultState(postcondition, navigation, observation),
            PostconditionCheck::kPending);

  navigation.committed = true;
  navigation.committed_origin = Tuple("https://primary.taffy.test");
  EXPECT_EQ(CheckSearchResultState(postcondition, navigation, observation),
            PostconditionCheck::kSatisfied);

  // A commit with no results yet is still pending, not verified: landing on
  // the results origin is not the same as results existing.
  EXPECT_EQ(
      CheckSearchResultState(postcondition, navigation, Observed(1, 2, {})),
      PostconditionCheck::kPending);
}

TEST(PostconditionChecksTest, ValueChangeUsesExactNodeMutationRevision) {
  Postcondition postcondition;
  postcondition.kind = PostconditionKind::kNodeValueChanged;

  ObservationEvidence evidence = Observed(1, 2, {});
  evidence.facts->value_changed_at_revision = 1;
  EXPECT_EQ(CheckNodeValueChanged(postcondition, evidence),
            PostconditionCheck::kPending);

  evidence.facts->value_changed_at_revision = 2;
  EXPECT_EQ(CheckNodeValueChanged(postcondition, evidence),
            PostconditionCheck::kSatisfied);

  postcondition.expected_value_digest =
      ContentDigest{DigestAlgorithm::kSha256, "forbidden"};
  EXPECT_EQ(CheckNodeValueChanged(postcondition, evidence),
            PostconditionCheck::kContradicted);
}

// A download bound to this dispatch, which is the only shape that settles
// anything. Written as a helper so that every case below states its one
// deviation from it rather than rebuilding the whole.
BrowserFlowEvidence DownloadBoundTo(const DispatchWatermark& dispatch) {
  DownloadFlowFacts facts;
  facts.download_id = 12u;
  facts.state = DownloadState::kInProgress;
  facts.attributed_to = dispatch;

  BrowserFlowEvidence evidence;
  evidence.flow_started = true;
  evidence.kind = BrowserFlowKind::kDownload;
  evidence.download = facts;
  return evidence;
}

TEST(PostconditionChecksTest, AFlowOfTheWrongKindIsContradicted) {
  constexpr DispatchWatermark kDispatch{7u};
  Postcondition postcondition;
  postcondition.kind = PostconditionKind::kBrowserFlowStarted;
  postcondition.expected_flow = BrowserFlowKind::kDownload;

  BrowserFlowEvidence evidence;
  EXPECT_EQ(CheckBrowserFlowStarted(postcondition, evidence, kDispatch),
            PostconditionCheck::kPending);

  evidence = DownloadBoundTo(kDispatch);
  EXPECT_EQ(CheckBrowserFlowStarted(postcondition, evidence, kDispatch),
            PostconditionCheck::kSatisfied);

  postcondition.expected_flow = BrowserFlowKind::kPermissionPrompt;
  EXPECT_EQ(CheckBrowserFlowStarted(postcondition, evidence, kDispatch),
            PostconditionCheck::kContradicted);
}

// The check's half of the rule the witness's watermark exists for. A download
// the person started in the same tab, seconds after the action went out, is
// scoped correctly, is of the right kind, and is not this action's effect.
TEST(PostconditionChecksTest, AnUnboundFlowSettlesNothing) {
  constexpr DispatchWatermark kDispatch{7u};
  Postcondition postcondition;
  postcondition.kind = PostconditionKind::kBrowserFlowStarted;
  postcondition.expected_flow = BrowserFlowKind::kDownload;

  BrowserFlowEvidence unattributed = DownloadBoundTo(kDispatch);
  unattributed.download->attributed_to.reset();
  EXPECT_EQ(CheckBrowserFlowStarted(postcondition, unattributed, kDispatch),
            PostconditionCheck::kPending)
      << "A download nobody could bind to this dispatch settled its "
         "postcondition, which is a kVerified the action did not earn.";

  // Pending rather than contradicted, and the difference matters: an unrelated
  // download is not a statement that the action failed either, so it must not
  // be able to refuse a postcondition it says nothing about.
  postcondition.expected_flow = BrowserFlowKind::kFileChooser;
  EXPECT_EQ(CheckBrowserFlowStarted(postcondition, unattributed, kDispatch),
            PostconditionCheck::kPending);
}

TEST(PostconditionChecksTest, AFlowBoundToAnotherDispatchSettlesNothing) {
  Postcondition postcondition;
  postcondition.kind = PostconditionKind::kBrowserFlowStarted;
  postcondition.expected_flow = BrowserFlowKind::kDownload;

  const BrowserFlowEvidence evidence = DownloadBoundTo(DispatchWatermark{7u});
  EXPECT_EQ(
      CheckBrowserFlowStarted(postcondition, evidence, DispatchWatermark{8u}),
      PostconditionCheck::kPending);

  // And a verifier that was never started holds a default watermark, which is
  // a value NoteDispatch never returns.
  EXPECT_EQ(
      CheckBrowserFlowStarted(postcondition, evidence, DispatchWatermark()),
      PostconditionCheck::kPending);
}

// Three of the four browser flows have no witness, so nothing can bind one,
// so nothing about one can be believed. This is what "refused at admission"
// looks like from inside the check.
TEST(PostconditionChecksTest, AWitnessLessFlowIsNeverSatisfied) {
  constexpr DispatchWatermark kDispatch{7u};
  Postcondition postcondition;
  postcondition.kind = PostconditionKind::kBrowserFlowStarted;

  for (const BrowserFlowKind kind :
       {BrowserFlowKind::kFileChooser, BrowserFlowKind::kPermissionPrompt,
        BrowserFlowKind::kExternalIntent}) {
    postcondition.expected_flow = kind;
    BrowserFlowEvidence evidence;
    evidence.flow_started = true;
    evidence.kind = kind;
    EXPECT_EQ(CheckBrowserFlowStarted(postcondition, evidence, kDispatch),
              PostconditionCheck::kPending);
  }
}

TEST(PostconditionChecksTest, BrowserOwnedClassesAreNamedOnce) {
  EXPECT_TRUE(
      IsBrowserObservedPostcondition(PostconditionKind::kCommittedNavigation));
  EXPECT_TRUE(
      IsBrowserObservedPostcondition(PostconditionKind::kNewTabCreated));
  EXPECT_TRUE(
      IsBrowserObservedPostcondition(PostconditionKind::kBrowserFlowStarted));
  EXPECT_TRUE(
      IsBrowserObservedPostcondition(PostconditionKind::kLoadingStopped));
  EXPECT_FALSE(IsBrowserObservedPostcondition(PostconditionKind::kNoMutation));
  EXPECT_FALSE(
      IsBrowserObservedPostcondition(PostconditionKind::kNodeStateChanged));
  // The two that read both kinds of evidence answer true to both predicates,
  // which is why they are two predicates and not one negation.
  EXPECT_TRUE(
      IsBrowserObservedPostcondition(PostconditionKind::kDocumentAdvanced));
  EXPECT_TRUE(PostconditionNeedsObservationEvidence(
      PostconditionKind::kDocumentAdvanced));
  EXPECT_TRUE(PostconditionNeedsObservationEvidence(
      PostconditionKind::kSearchResultState));
  EXPECT_FALSE(PostconditionNeedsObservationEvidence(
      PostconditionKind::kCommittedNavigation));
  EXPECT_FALSE(
      PostconditionNeedsObservationEvidence(PostconditionKind::kLoadingStopped));
  EXPECT_TRUE(
      PostconditionNeedsObservationEvidence(PostconditionKind::kNoMutation));
}

TEST(PostconditionChecksTest, AnOrdinaryPressClaimsOnlyThatTheDocumentMoved) {
  Postcondition postcondition;
  postcondition.kind = PostconditionKind::kDocumentAdvanced;
  // No operand of any kind is needed: the claim is complete on its own.
  EXPECT_TRUE(PostconditionCarriesAnObservableClaim(postcondition));

  ObservationEvidence observation;
  observation.dispatch_revision = 7u;
  NavigationEvidence navigation;
  EXPECT_EQ(PostconditionCheck::kPending,
            CheckDocumentAdvanced(postcondition, observation, navigation));

  // A graph standing exactly where it was at dispatch is a page that has not
  // moved yet, not a page that refused to. The deadline ends that wait as a
  // timeout, which is the honest name for "the control did nothing".
  observation.attempted = true;
  observation.node_resolved = true;
  observation.handle_still_live = true;
  observation.observed_at_revision = 7u;
  EXPECT_EQ(PostconditionCheck::kPending,
            CheckDocumentAdvanced(postcondition, observation, navigation));

  observation.observed_at_revision = 8u;
  EXPECT_EQ(PostconditionCheck::kSatisfied,
            CheckDocumentAdvanced(postcondition, observation, navigation));
}

TEST(PostconditionChecksTest, ADeadTargetOrAnyCommitIsTheDocumentMoving) {
  Postcondition postcondition;
  postcondition.kind = PostconditionKind::kDocumentAdvanced;

  ObservationEvidence gone;
  gone.dispatch_revision = 7u;
  gone.attempted = true;
  gone.node_gone = true;
  EXPECT_EQ(PostconditionCheck::kSatisfied,
            CheckDocumentAdvanced(postcondition, gone, NavigationEvidence()));

  ObservationEvidence dead_handle;
  dead_handle.dispatch_revision = 7u;
  dead_handle.attempted = true;
  dead_handle.node_resolved = true;
  dead_handle.handle_still_live = false;
  EXPECT_EQ(PostconditionCheck::kSatisfied,
            CheckDocumentAdvanced(postcondition, dead_handle,
                                  NavigationEvidence()));

  // An error document is still the page the control was on being gone. The
  // caller reads what loaded; calling this a contradiction would end the
  // action on a code that means never try anything like this again.
  NavigationEvidence error_page;
  error_page.committed = true;
  error_page.is_error_page = true;
  EXPECT_EQ(PostconditionCheck::kSatisfied,
            CheckDocumentAdvanced(postcondition, ObservationEvidence(),
                                  error_page));
}

}  // namespace
}  // namespace taffy

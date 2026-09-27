// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/observed_link_registry.h"

#include <string>
#include <utility>
#include <vector>

#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

constexpr char kDestination[] =
    "https://destination.test/path?q=tea%20cake#result";

ObservationEnvelope CompleteObservation() {
  ObservationEnvelope observation;
  observation.code = ObservationResultCode::kOk;
  observation.tab_id = TabId{"tab-1"};
  observation.root_frame_id = FrameId{"frame-1"};
  observation.page_epoch = PageEpoch{"epoch-1"};
  observation.graph_revision = 7u;
  observation.lifecycle_state = DocumentLifecycleState::kActive;
  observation.origin.kind = OriginKind::kTuple;
  observation.origin.serialization = "https://source.test";
  observation.node_count = 1u;
  observation.encoding = GraphPayloadEncoding::kBipContract;
  observation.transient_observed_links.push_back(TransientObservedLink{
      .node_id = SemanticNodeId{"node-1"},
      .normalized_destination = kDestination,
  });
  return observation;
}

CanonicalLinkOpenHandle ExactHandle() {
  return CanonicalLinkOpenHandle{
      .tab_id = "tab-1",
      .frame_id = "frame-1",
      .page_epoch = "epoch-1",
      .graph_revision = 7u,
      .node_id = "node-1",
      .expected_origin_is_opaque = false,
      .expected_origin = "https://source.test",
  };
}

// A download link is not a navigation and resolves only for the download move.
// A `target="_blank"` link is an ordinary navigation whose target this path
// never reads, and refusing it left it unreachable by any move (decision 0184).
TEST(ObservedLinkRegistryTest, OnlyADownloadLinkIsWithheldFromLinkOpen) {
  for (int flags = 0; flags <= 3; ++flags) {
    SCOPED_TRACE(flags);
    auto observation = CompleteObservation();
    auto& link = observation.transient_observed_links.front();
    link.is_download = (flags & 1) != 0;
    link.opens_new_tab = (flags & 2) != 0;
    ObservedLinkRegistry registry;
    ASSERT_TRUE(registry.Replace(observation));
    EXPECT_EQ(registry.ResolveDownload(ExactHandle()), kDestination);
    const std::optional<std::string> navigation =
        (flags & 1) == 0 ? std::optional<std::string>(kDestination)
                         : std::nullopt;
    EXPECT_EQ(registry.Resolve(ExactHandle()), navigation);
    EXPECT_EQ(registry.ResolveObservedLink(NodeHandle{
                  .tab_id = observation.tab_id,
                  .frame_id = observation.root_frame_id,
                  .page_epoch = observation.page_epoch,
                  .graph_revision = observation.graph_revision,
                  .node_id = link.node_id,
                  .expected_origin = observation.origin,
              }),
              navigation);
  }
}

TEST(ObservedLinkRegistryTest, DownloadRequiresHttpsWithoutChangingNavigation) {
  auto observation = CompleteObservation();
  observation.transient_observed_links.front().normalized_destination =
      "http://destination.test/file";
  ObservedLinkRegistry registry;
  ASSERT_TRUE(registry.Replace(observation));
  EXPECT_EQ(registry.Resolve(ExactHandle()), "http://destination.test/file");
  EXPECT_FALSE(registry.ResolveDownload(ExactHandle()));
}

TEST(ObservedLinkRegistryTest, ResolvesOnlyExactCompleteSnapshotHandle) {
  ObservedLinkRegistry registry;
  EXPECT_TRUE(registry.Replace(CompleteObservation()));
  EXPECT_EQ(registry.Resolve(ExactHandle()), kDestination);
  EXPECT_EQ(registry.ResolveDownload(ExactHandle()), kDestination);

  CanonicalLinkOpenHandle changed = ExactHandle();
  changed.tab_id = "tab-2";
  EXPECT_FALSE(registry.Resolve(changed));
  EXPECT_FALSE(registry.ResolveDownload(changed));
  changed = ExactHandle();
  changed.frame_id = "frame-2";
  EXPECT_FALSE(registry.Resolve(changed));
  EXPECT_FALSE(registry.ResolveDownload(changed));
  changed = ExactHandle();
  changed.page_epoch = "epoch-2";
  EXPECT_FALSE(registry.Resolve(changed));
  EXPECT_FALSE(registry.ResolveDownload(changed));
  changed = ExactHandle();
  changed.graph_revision = 8u;
  EXPECT_FALSE(registry.Resolve(changed));
  EXPECT_FALSE(registry.ResolveDownload(changed));
  changed = ExactHandle();
  changed.node_id = "node-2";
  EXPECT_FALSE(registry.Resolve(changed));
  EXPECT_FALSE(registry.ResolveDownload(changed));
  changed = ExactHandle();
  changed.expected_origin = "https://other.test";
  EXPECT_FALSE(registry.Resolve(changed));
  EXPECT_FALSE(registry.ResolveDownload(changed));
  changed = ExactHandle();
  changed.expected_origin_is_opaque = true;
  EXPECT_FALSE(registry.Resolve(changed));
  EXPECT_FALSE(registry.ResolveDownload(changed));
}

TEST(ObservedLinkRegistryTest, FailedReplacementRevokesOlderAuthority) {
  ObservedLinkRegistry registry;
  EXPECT_TRUE(registry.Replace(CompleteObservation()));
  ObservationEnvelope failed = CompleteObservation();
  failed.code = ObservationResultCode::kUnsupported;
  EXPECT_FALSE(registry.Replace(failed));
  EXPECT_FALSE(registry.Resolve(ExactHandle()));
  EXPECT_FALSE(registry.ResolveDownload(ExactHandle()));
  EXPECT_EQ(registry.size_for_testing(), 0u);
}

TEST(ObservedLinkRegistryTest, AReadingThatStoppedEarlyStillRegisters) {
  for (ObservationResultCode code : {ObservationResultCode::kConflicted,
                                     ObservationResultCode::kIncomplete}) {
    SCOPED_TRACE(static_cast<int>(code));
    ObservedLinkRegistry registry;
    ObservationEnvelope incomplete = CompleteObservation();
    incomplete.code = code;
    incomplete.truncation.truncated = true;
    incomplete.truncation.omitted_node_count = 96u;
    EXPECT_TRUE(registry.Replace(incomplete));
    EXPECT_EQ(registry.Resolve(ExactHandle()), std::string(kDestination));
    EXPECT_EQ(registry.size_for_testing(), 1u);
  }
}

TEST(ObservedLinkRegistryTest, EveryCodeAboveAReadingIsStillRefused) {
  for (ObservationResultCode code :
       {ObservationResultCode::kBudgetExceeded,
        ObservationResultCode::kDeadlineExceeded,
        ObservationResultCode::kResourcePressure,
        ObservationResultCode::kStalePageEpoch,
        ObservationResultCode::kDocumentInactive,
        ObservationResultCode::kCancelled, ObservationResultCode::kUnsupported,
        ObservationResultCode::kInternalError}) {
    SCOPED_TRACE(static_cast<int>(code));
    ObservedLinkRegistry registry;
    ObservationEnvelope refused = CompleteObservation();
    refused.code = code;
    EXPECT_FALSE(registry.Replace(refused));
    EXPECT_EQ(registry.size_for_testing(), 0u);
  }
}

TEST(ObservedLinkRegistryTest, DuplicateOrRestrictedRowsFailClosed) {
  ObservedLinkRegistry registry;
  ObservationEnvelope duplicate = CompleteObservation();
  duplicate.node_count = 2u;
  duplicate.transient_observed_links.push_back(
      duplicate.transient_observed_links.front());
  EXPECT_FALSE(registry.Replace(duplicate));
  EXPECT_FALSE(registry.ResolveDownload(ExactHandle()));

  ObservationEnvelope restricted = CompleteObservation();
  restricted.transient_observed_links.front().normalized_destination =
      "javascript:alert(1)";
  EXPECT_FALSE(registry.Replace(restricted));
  EXPECT_FALSE(registry.ResolveDownload(ExactHandle()));

  ObservationEnvelope credentials = CompleteObservation();
  credentials.transient_observed_links.front().normalized_destination =
      "https://person:secret@destination.test/";
  EXPECT_FALSE(registry.Replace(credentials));
  EXPECT_FALSE(registry.ResolveDownload(ExactHandle()));
}

TEST(ObservedLinkRegistryTest, NewProcessStartsWithoutInheritedAuthority) {
  ObservedLinkRegistry first_process;
  EXPECT_TRUE(first_process.Replace(CompleteObservation()));
  EXPECT_TRUE(first_process.Resolve(ExactHandle()));
  EXPECT_TRUE(first_process.ResolveDownload(ExactHandle()));

  ObservedLinkRegistry restarted_process;
  EXPECT_FALSE(restarted_process.Resolve(ExactHandle()));
  EXPECT_FALSE(restarted_process.ResolveDownload(ExactHandle()));
}

DeltaEnvelope RemovalDelta() {
  DeltaEnvelope delta;
  delta.tab_id = TabId{"tab-1"};
  delta.frame_id = FrameId{"frame-1"};
  delta.page_epoch = PageEpoch{"epoch-1"};
  delta.from_revision = 7u;
  delta.to_revision = 8u;
  delta.removed_node_count = 1u;
  delta.removed_node_ids.push_back(SemanticNodeId{"node-2"});
  return delta;
}

ObservationEnvelope TwoLinkObservation() {
  ObservationEnvelope observation = CompleteObservation();
  observation.node_count = 2u;
  observation.transient_observed_links.push_back(TransientObservedLink{
      .node_id = SemanticNodeId{"node-2"},
      .normalized_destination = "https://destination.test/other",
  });
  return observation;
}

TEST(ObservedLinkRegistryTest, DeltaRetiresOnlyTheNodesItNames) {
  ObservedLinkRegistry registry;
  ASSERT_TRUE(registry.Replace(TwoLinkObservation()));
  ASSERT_TRUE(registry.ApplyDelta(RemovalDelta()));

  // The surviving link resolves both at the revision it was read at and at
  // the one the delta advanced to; the retired one resolves at neither.
  EXPECT_EQ(registry.Resolve(ExactHandle()), kDestination);
  CanonicalLinkOpenHandle advanced = ExactHandle();
  advanced.graph_revision = 8u;
  EXPECT_EQ(registry.Resolve(advanced), kDestination);
  EXPECT_EQ(registry.size_for_testing(), 1u);

  CanonicalLinkOpenHandle removed = ExactHandle();
  removed.node_id = "node-2";
  EXPECT_FALSE(registry.Resolve(removed));
}

TEST(ObservedLinkRegistryTest, NoHandleFromAFutureRevisionIsAdmitted) {
  ObservedLinkRegistry registry;
  ASSERT_TRUE(registry.Replace(TwoLinkObservation()));
  ASSERT_TRUE(registry.ApplyDelta(RemovalDelta()));

  CanonicalLinkOpenHandle ahead = ExactHandle();
  ahead.graph_revision = 9u;
  EXPECT_FALSE(registry.Resolve(ahead));
  EXPECT_FALSE(registry.ResolveDownload(ahead));
}

// A handle older than the reading this table was built from resolves
// (decision 0185). It used to be refused, and that refusal fired on the
// ordinary case: the walk re-reads a page before each paid turn, so the table
// is replaced between the reading the model was shown and the move it asks
// for from that reading. A phone measured `window=2833..2833 asked=1`.
//
// A second `Replace` is what the old floor was about, so the case is written
// with one rather than with a hand-set revision: this is a whole new reading
// of the same document, exactly the situation the rule named.
TEST(ObservedLinkRegistryTest, AHandleOlderThanThisReadingStillResolves) {
  ObservedLinkRegistry registry;
  ASSERT_TRUE(registry.Replace(CompleteObservation()));
  const CanonicalLinkOpenHandle as_the_model_read_it = ExactHandle();
  ASSERT_EQ(registry.Resolve(as_the_model_read_it), kDestination);

  ObservationEnvelope reread = CompleteObservation();
  reread.graph_revision = 2833u;
  ASSERT_TRUE(registry.Replace(reread));

  EXPECT_EQ(registry.Resolve(as_the_model_read_it), kDestination);
  EXPECT_EQ(registry.ResolveDownload(as_the_model_read_it), kDestination);
}

// And what actually carries the property the floor was believed to carry.
//
// The floor never compared an address: it compared when this tab was last
// observed. What says a handle still names the link the model picked is that
// the node is in the table the current reading built — an identifier is never
// reissued inside a page epoch, so a node that is gone from the new reading
// is gone, and one that is present is the same node it was. A re-read that
// drops the node refuses the old handle for that reason and names it.
TEST(ObservedLinkRegistryTest, AnOldHandleForANodeTheRereadDroppedIsRefused) {
  ObservedLinkRegistry registry;
  ASSERT_TRUE(registry.Replace(TwoLinkObservation()));
  CanonicalLinkOpenHandle second_link = ExactHandle();
  second_link.node_id = "node-2";
  ASSERT_TRUE(registry.Resolve(second_link));

  ObservationEnvelope without_it = CompleteObservation();
  without_it.graph_revision = 2833u;
  ASSERT_TRUE(registry.Replace(without_it));

  EXPECT_FALSE(registry.Resolve(second_link));
  EXPECT_FALSE(registry.ResolveDownload(second_link));
  // The link the re-read kept is unaffected, so this is the node and not the
  // revision doing the work.
  EXPECT_EQ(registry.Resolve(ExactHandle()), kDestination);
}

// A different document is still a different document. The page epoch is the
// bound that survived, and it is the one the node-identity invariant is
// stated against: ids are unique within an epoch and say nothing across one.
TEST(ObservedLinkRegistryTest, AnOldHandleFromAnotherPageEpochIsStillRefused) {
  ObservedLinkRegistry registry;
  ASSERT_TRUE(registry.Replace(CompleteObservation()));
  ObservationEnvelope next_document = CompleteObservation();
  next_document.page_epoch = PageEpoch{"epoch-2"};
  next_document.graph_revision = 2833u;
  ASSERT_TRUE(registry.Replace(next_document));

  EXPECT_FALSE(registry.Resolve(ExactHandle()));
  EXPECT_FALSE(registry.ResolveDownload(ExactHandle()));
}

TEST(ObservedLinkRegistryTest, DeltaThisBuildCannotAccountForStillClears) {
  const auto cases = std::vector<std::pair<const char*, DeltaEnvelope>>{
      {"revision gap",
       [] {
         DeltaEnvelope gap = RemovalDelta();
         gap.from_revision = 6u;
         return gap;
       }()},
      {"other epoch",
       [] {
         DeltaEnvelope epoch = RemovalDelta();
         epoch.page_epoch = PageEpoch{"epoch-2"};
         return epoch;
       }()},
      {"other tab",
       [] {
         DeltaEnvelope tab = RemovalDelta();
         tab.tab_id = TabId{"tab-2"};
         return tab;
       }()},
      {"changed node",
       [] {
         DeltaEnvelope changed = RemovalDelta();
         changed.changed_node_count = 1u;
         return changed;
       }()},
      {"changed edge",
       [] {
         DeltaEnvelope edge = RemovalDelta();
         edge.changed_edge_count = 1u;
         return edge;
       }()},
      {"truncated",
       [] {
         DeltaEnvelope truncated = RemovalDelta();
         truncated.truncation.truncated = true;
         return truncated;
       }()},
      {"count disagrees with the list",
       [] {
         DeltaEnvelope miscounted = RemovalDelta();
         miscounted.removed_node_count = 2u;
         return miscounted;
       }()},
  };
  for (const auto& [name, delta] : cases) {
    SCOPED_TRACE(name);
    ObservedLinkRegistry registry;
    ASSERT_TRUE(registry.Replace(TwoLinkObservation()));
    EXPECT_FALSE(registry.ApplyDelta(delta));
    EXPECT_FALSE(registry.Resolve(ExactHandle()));
    EXPECT_EQ(registry.size_for_testing(), 0u);
  }
}

TEST(ObservedLinkRegistryTest, AddedNodesLeaveTheTableStanding) {
  DeltaEnvelope added = RemovalDelta();
  added.added_node_count = 3u;
  ObservedLinkRegistry registry;
  ASSERT_TRUE(registry.Replace(TwoLinkObservation()));
  EXPECT_TRUE(registry.ApplyDelta(added));
  EXPECT_EQ(registry.Resolve(ExactHandle()), kDestination);
}

// A child frame navigating inside the page — an advertisement, an embedded
// video, a CAPTCHA widget — says nothing about the root frame's links, and a
// phone lost them to exactly that between a reading and the link it opened.
// The document they were read from moving still takes them.
TEST(ObservedLinkRegistryTest, OnlyANoticeAboutThisDocumentTakesItsLinks) {
  ObservedLinkRegistry registry;
  ASSERT_TRUE(registry.Replace(CompleteObservation()));

  InvalidationNotice child;
  child.tab_id = TabId{"tab-1"};
  child.frame_id = FrameId{"frame-2"};
  child.page_epoch = PageEpoch{"epoch-9"};
  child.reason = InvalidationCode::kChildFrameNavigation;
  child.invalidates_child_frames_only = true;
  EXPECT_TRUE(registry.Invalidate(child));
  EXPECT_EQ(registry.Resolve(ExactHandle()), kDestination);

  InvalidationNotice root;
  root.tab_id = TabId{"tab-1"};
  root.frame_id = FrameId{"frame-1"};
  root.page_epoch = PageEpoch{"epoch-1"};
  root.reason = InvalidationCode::kHistoryRouteChange;
  EXPECT_FALSE(registry.Invalidate(root));
  EXPECT_EQ(registry.Resolve(ExactHandle()), std::nullopt);
}

}  // namespace
}  // namespace taffy

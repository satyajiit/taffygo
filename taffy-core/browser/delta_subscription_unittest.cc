// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/components/intelligence/content/delta_subscription.h"

#include <memory>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "testing/gtest/include/gtest/gtest.h"

// One subscription's promise: every delta it hands onward applies exactly to
// the projection the subscriber last acknowledged, and nothing partial is ever
// applied (protocol section 10).

namespace taffy {
namespace {

constexpr char kEpoch[] = "epoch_1";

SubscriptionRequest GrantedRequest(uint32_t queue_depth,
                                   uint32_t max_delta_bytes) {
  SubscriptionRequest request;
  request.task_id = TaskId{"task_1"};
  request.tab_id = TabId{"tab_1"};
  request.frame_id = FrameId{"frame_1"};
  request.expected_page_epoch = PageEpoch{kEpoch};
  request.scope = ObservationScope::kDocument;
  request.include_text_deltas = true;
  request.include_layout_deltas = true;
  request.budget.max_queue_depth = queue_depth;
  request.budget.max_queued_bytes = 1024 * 1024;
  request.budget.max_delta_bytes = max_delta_bytes;
  request.budget.coalescing_window_ms = 100;
  return request;
}

DeltaEnvelope Delta(GraphRevision from,
                    GraphRevision to,
                    EventSequence sequence,
                    size_t payload_bytes = 8) {
  DeltaEnvelope delta;
  delta.page_epoch = PageEpoch{kEpoch};
  delta.from_revision = from;
  delta.to_revision = to;
  delta.event_sequence = sequence;
  delta.graph_payload.assign(payload_bytes, 0u);
  return delta;
}

class Recorder {
 public:
  DeltaSubscription::DeliverCallback Callback() {
    return base::BindRepeating(&Recorder::Receive, base::Unretained(this));
  }
  void Receive(DeltaEnvelope delta) { delivered.push_back(std::move(delta)); }

  std::vector<DeltaEnvelope> delivered;
};

std::unique_ptr<DeltaSubscription> MakeSubscription(
    Recorder* recorder,
    uint32_t queue_depth = 8,
    uint32_t max_delta_bytes = 1024) {
  return std::make_unique<DeltaSubscription>(
      SubscriptionId{"sub_1"}, TabId{"tab_1"}, FrameId{"frame_1"},
      PageEpoch{kEpoch}, /*base_revision=*/1,
      GrantedRequest(queue_depth, max_delta_bytes), recorder->Callback());
}

TEST(DeltaSubscriptionTest, DeliversAMatchingDeltaAndAdvancesTheCursor) {
  Recorder recorder;
  auto subscription = MakeSubscription(&recorder);

  EXPECT_EQ(subscription->Offer(Delta(1, 2, 1)), DeltaRejectReason::kNone);
  ASSERT_EQ(recorder.delivered.size(), 1u);
  EXPECT_EQ(subscription->cursor().revision, 2u);
  EXPECT_EQ(subscription->cursor().event_sequence, 1u);
  EXPECT_EQ(subscription->state(), SubscriptionState::kActive);
}

TEST(DeltaSubscriptionTest, DiscardsADuplicateWithoutKillingTheProjection) {
  Recorder recorder;
  auto subscription = MakeSubscription(&recorder);
  ASSERT_EQ(subscription->Offer(Delta(1, 2, 1)), DeltaRejectReason::kNone);

  EXPECT_EQ(subscription->Offer(Delta(1, 2, 1)), DeltaRejectReason::kDuplicate);
  EXPECT_EQ(recorder.delivered.size(), 1u);
  EXPECT_EQ(subscription->state(), SubscriptionState::kActive);
  EXPECT_EQ(subscription->counters().discarded_duplicate, 1u);
}

TEST(DeltaSubscriptionTest, AGapForcesAResnapshotAndStopsDelivery) {
  Recorder recorder;
  auto subscription = MakeSubscription(&recorder);
  ASSERT_EQ(subscription->Offer(Delta(1, 2, 1)), DeltaRejectReason::kNone);

  EXPECT_EQ(subscription->Offer(Delta(2, 3, 4)),
            DeltaRejectReason::kSequenceGap);
  EXPECT_EQ(subscription->state(), SubscriptionState::kAwaitingResnapshot);

  // Nothing is delivered while the projection is dead, and nothing is queued
  // either: it would be work the resnapshot is about to throw away.
  EXPECT_EQ(subscription->Offer(Delta(3, 4, 5)),
            DeltaRejectReason::kSubscriptionNotApplying);
  EXPECT_EQ(recorder.delivered.size(), 1u);
  EXPECT_EQ(subscription->queue_depth(), 0u);
}

TEST(DeltaSubscriptionTest, ADeltaFromAnotherDocumentIsRefused) {
  Recorder recorder;
  auto subscription = MakeSubscription(&recorder);

  DeltaEnvelope foreign = Delta(1, 2, 1);
  foreign.page_epoch = PageEpoch{"epoch_2"};
  EXPECT_EQ(subscription->Offer(std::move(foreign)),
            DeltaRejectReason::kEpochMismatch);
  EXPECT_TRUE(recorder.delivered.empty());
  EXPECT_EQ(subscription->state(), SubscriptionState::kAwaitingResnapshot);
}

TEST(DeltaSubscriptionTest, ARevisionMismatchIsRefusedRatherThanReconciled) {
  Recorder recorder;
  auto subscription = MakeSubscription(&recorder);
  // The subscriber holds revision 1; this delta starts from 5.
  EXPECT_EQ(subscription->Offer(Delta(5, 6, 1)),
            DeltaRejectReason::kRevisionMismatch);
  EXPECT_TRUE(recorder.delivered.empty());
  EXPECT_EQ(subscription->counters().rejected_revision_mismatch, 1u);
}

TEST(DeltaSubscriptionTest, AnOversizedDeltaIsRefusedRatherThanTruncated) {
  Recorder recorder;
  auto subscription = MakeSubscription(&recorder, /*queue_depth=*/8,
                                       /*max_delta_bytes=*/16);
  EXPECT_EQ(subscription->Offer(Delta(1, 2, 1, /*payload_bytes=*/64)),
            DeltaRejectReason::kOverBudget);
  // Truncating would hand the subscriber a partial application, which is the
  // outcome protocol section 10 forbids outright.
  EXPECT_TRUE(recorder.delivered.empty());
  EXPECT_EQ(subscription->state(), SubscriptionState::kAwaitingResnapshot);
}

TEST(DeltaSubscriptionTest, QueueDepthReflectsUnacknowledgedDeltas) {
  Recorder recorder;
  auto subscription = MakeSubscription(&recorder);
  ASSERT_EQ(subscription->Offer(Delta(1, 2, 1)), DeltaRejectReason::kNone);
  ASSERT_EQ(subscription->Offer(Delta(2, 3, 2)), DeltaRejectReason::kNone);
  EXPECT_EQ(subscription->queue_depth(), 2u);

  subscription->Acknowledge(1);
  EXPECT_EQ(subscription->queue_depth(), 1u);
  subscription->Acknowledge(2);
  EXPECT_EQ(subscription->queue_depth(), 0u);
  EXPECT_EQ(subscription->queued_bytes(), 0u);
}

TEST(DeltaSubscriptionTest, PausingHoldsDeltasAndResumingFlushesThemInOrder) {
  Recorder recorder;
  auto subscription = MakeSubscription(&recorder);
  ASSERT_EQ(subscription->Offer(Delta(1, 2, 1)), DeltaRejectReason::kNone);
  ASSERT_EQ(recorder.delivered.size(), 1u);

  const std::optional<BackpressureNotice> paused = subscription->Apply(
      BackpressureAction::kSubscriptionPaused, DeltaShedPlan(), std::nullopt,
      /*resnapshot_required=*/false, SubscriptionState::kPaused, 100);
  ASSERT_TRUE(paused.has_value());
  EXPECT_EQ(subscription->state(), SubscriptionState::kPaused);

  ASSERT_EQ(subscription->Offer(Delta(2, 3, 2)), DeltaRejectReason::kNone);
  ASSERT_EQ(subscription->Offer(Delta(3, 4, 3)), DeltaRejectReason::kNone);
  // Held, not dropped: a pause keeps the projection alive.
  EXPECT_EQ(recorder.delivered.size(), 1u);
  EXPECT_EQ(subscription->queue_depth(), 3u);

  subscription->Apply(BackpressureAction::kCoalesced, DeltaShedPlan(),
                      std::nullopt, /*resnapshot_required=*/false,
                      SubscriptionState::kActive, 200);
  ASSERT_EQ(recorder.delivered.size(), 3u);
  EXPECT_EQ(recorder.delivered[1].event_sequence, 2u);
  EXPECT_EQ(recorder.delivered[2].event_sequence, 3u);
}

TEST(DeltaSubscriptionTest, SheddingTextStopsCarryingIt) {
  Recorder recorder;
  auto subscription = MakeSubscription(&recorder);
  ASSERT_TRUE(subscription->carries_text_deltas());

  DeltaShedPlan plan;
  plan.classes.push_back(*SheddableDeltaClass::From(DeltaClass::kText));
  const std::optional<BackpressureNotice> notice = subscription->Apply(
      BackpressureAction::kCoalesced, plan, std::nullopt,
      /*resnapshot_required=*/false, SubscriptionState::kActive, 100);

  ASSERT_TRUE(notice.has_value());
  EXPECT_FALSE(subscription->carries_text_deltas());
  EXPECT_TRUE(subscription->carries_layout_deltas());
  ASSERT_EQ(notice->dropped_classes.size(), 1u);
  EXPECT_EQ(notice->dropped_classes.front(), DeltaClass::kText);
  EXPECT_TRUE(BackpressureNoticeIsWellFormed(*notice));
}

TEST(DeltaSubscriptionTest, ScopeReductionIsRecordedOnceAndNotOfferedTwice) {
  Recorder recorder;
  auto subscription = MakeSubscription(&recorder);
  EXPECT_FALSE(subscription->scope_already_reduced());

  subscription->Apply(BackpressureAction::kScopeReduced, DeltaShedPlan(),
                      ObservationScope::kViewport,
                      /*resnapshot_required=*/false,
                      SubscriptionState::kActive, 100);
  EXPECT_TRUE(subscription->scope_already_reduced());
  EXPECT_EQ(subscription->scope(), ObservationScope::kViewport);
}

TEST(DeltaSubscriptionTest, RebaseIsTheOnlyWayOutOfAwaitingResnapshot) {
  Recorder recorder;
  auto subscription = MakeSubscription(&recorder);
  ASSERT_EQ(subscription->Offer(Delta(1, 2, 1)), DeltaRejectReason::kNone);
  ASSERT_EQ(subscription->Offer(Delta(2, 3, 9)),
            DeltaRejectReason::kSequenceGap);
  ASSERT_EQ(subscription->state(), SubscriptionState::kAwaitingResnapshot);

  subscription->Rebase(/*revision=*/40, /*event_sequence=*/12);
  EXPECT_EQ(subscription->state(), SubscriptionState::kActive);
  EXPECT_EQ(subscription->cursor().revision, 40u);

  // The stream picks up from where the snapshot said it was, not from zero.
  EXPECT_EQ(subscription->Offer(Delta(40, 41, 13)), DeltaRejectReason::kNone);
  EXPECT_EQ(recorder.delivered.size(), 2u);
}

TEST(DeltaSubscriptionTest, StopIsTerminal) {
  Recorder recorder;
  auto subscription = MakeSubscription(&recorder);
  subscription->Stop();
  EXPECT_EQ(subscription->state(), SubscriptionState::kStopped);
  EXPECT_EQ(subscription->Offer(Delta(1, 2, 1)),
            DeltaRejectReason::kSubscriptionNotApplying);
  EXPECT_TRUE(recorder.delivered.empty());
  // A stopped subscription emits nothing further, not even a notice.
  EXPECT_FALSE(subscription
                   ->Apply(BackpressureAction::kCoalesced, DeltaShedPlan(),
                           std::nullopt, /*resnapshot_required=*/false,
                           SubscriptionState::kActive, 100)
                   .has_value());
}

}  // namespace
}  // namespace taffy

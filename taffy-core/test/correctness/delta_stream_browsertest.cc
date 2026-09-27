// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <string>
#include <utility>

#include "taffy/components/intelligence/content/page_intelligence_broker.h"
#include "taffy/common/public/bip_delta.h"
#include "taffy/common/public/bip_observation.h"
#include "taffy/common/public/bip_subscription.h"
#include "taffy/test/support/audit_stream_assertions.h"
#include "taffy/test/support/taffy_observation_test_base.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "content/shell/browser/shell.h"
#include "testing/gtest/include/gtest/gtest.h"

// The delta round trip, renderer to browser to core service.
//
// Everything else in the protocol works with no renderer endpoint at all. The
// stream does not: it needs a real endpoint in the same binary so Subscribe can
// answer, and until this suite runs the honest statement about the delta path
// is that its refusals are tested and its happy path is not.
//
// The order below is the order the browser directory's own verification list
// asks for, because each step's failure mode is invisible in the next: a
// subscription that never opened cannot produce a gap, and a stream that never
// delivered cannot be shown to rebase.

namespace taffy::test {
namespace {

class DeltaStreamTest : public TaffyObservationTestBase {
 protected:
  // Opens a subscription on the current main frame at the epoch an observation
  // just reported. Returns the envelope so a caller can assert on what it was
  // actually granted, which is never simply what it asked for.
  SubscriptionEnvelope SubscribeToCurrentPage(
      const ObservationEnvelope& observed) {
    return client().Subscribe(
        builder().Subscription(observed.root_frame_id, observed.page_epoch));
  }

  // Makes the page mutate in a way the adapters must notice.
  void MutateThePage() {
    ASSERT_TRUE(content::ExecJs(
        web_contents(),
        "const p = document.createElement('p');"
        "p.id = 'taffy-delta-marker';"
        "p.textContent = 'delta stream marker';"
        "document.body.appendChild(p);"));
  }
};

// Step one. Subscribe answers with a subscription identifier and the revision
// the projection starts from. A subscriber that has not taken a snapshot at
// that revision cannot apply the first delta, so the broker has to say what it
// is rather than leaving the subscriber to guess.
IN_PROC_BROWSER_TEST_F(DeltaStreamTest, SubscribeReportsItsBaseRevision) {
  const ObservationEnvelope observed = ObserveFixture("mutation-race");
  ASSERT_TRUE(observed.page_epoch.is_valid());

  const SubscriptionEnvelope subscription = SubscribeToCurrentPage(observed);
  ASSERT_EQ(ObservationResultCode::kOk, subscription.code)
      << "Subscribe was refused; every step below is about a stream that does "
         "not exist.";
  ASSERT_TRUE(subscription.subscription_id.has_value());
  ASSERT_TRUE(subscription.base_revision.has_value());
  EXPECT_GT(subscription.base_revision.value(), 0u)
      << "A base revision of zero means 'no revision', which no delta can be "
         "applied against.";
  EXPECT_EQ(observed.page_epoch, subscription.page_epoch.value());
}

// Step one continued. The granted budget is reported, and it is what the
// request was narrowed to rather than what it asked for. A subscriber that
// could not see the difference would size its own queues wrongly.
IN_PROC_BROWSER_TEST_F(DeltaStreamTest, TheGrantedBudgetIsReported) {
  const ObservationEnvelope observed = ObserveFixture("mutation-race");

  SubscriptionRequest request =
      builder().Subscription(observed.root_frame_id, observed.page_epoch);
  // Deliberately absurd. The broker clamps to the process ceiling and then to
  // the grant, and the envelope has to say so.
  request.budget.max_queue_depth = 1u << 20;
  request.budget.max_queued_bytes = 1u << 28;
  request.budget.coalescing_window_ms = 0;

  const SubscriptionEnvelope subscription = client().Subscribe(std::move(request));
  ASSERT_EQ(ObservationResultCode::kOk, subscription.code);
  EXPECT_LT(subscription.granted_budget.max_queue_depth, 1u << 20)
      << "A subscription was granted the queue depth it asked for. A request "
         "can never widen the process ceiling.";
  EXPECT_GT(subscription.granted_budget.coalescing_window_ms, 0u)
      << "The coalescing window is the one axis that widens rather than "
         "narrows: it is raised to the endpoint's floor, because asking for "
         "updates faster than the endpoint produces them does not make them "
         "arrive faster.";
}

// Step two. One delta arrives, and it applies to the projection the subscriber
// holds: same epoch, from-revision matching the cursor, sequence in order.
IN_PROC_BROWSER_TEST_F(DeltaStreamTest, OneDeltaArrivesAndApplies) {
  const ObservationEnvelope observed = ObserveFixture("mutation-race");
  const SubscriptionEnvelope subscription = SubscribeToCurrentPage(observed);
  ASSERT_EQ(ObservationResultCode::kOk, subscription.code);

  MutateThePage();
  ASSERT_TRUE(client().WaitForDeltaCount(1))
      << "No delta arrived after a DOM mutation on a subscribed frame.";

  const DeltaEnvelope& delta = client().deltas().front();
  EXPECT_EQ(observed.page_epoch, delta.page_epoch);
  EXPECT_EQ(subscription.base_revision.value(), delta.from_revision)
      << "The first delta does not start from the revision the subscription "
         "reported, so the subscriber's projection and the stream disagree "
         "about where they are.";
  EXPECT_GT(delta.to_revision, delta.from_revision);
  EXPECT_GT(delta.event_sequence, 0u);

  DeltaProjectionCursor cursor;
  cursor.page_epoch = observed.page_epoch;
  cursor.revision = subscription.base_revision.value();
  cursor.event_sequence = delta.event_sequence - 1;
  EXPECT_EQ(DeltaRejectReason::kNone,
            DeltaApplicability(cursor, delta.page_epoch, delta.from_revision,
                               delta.to_revision, delta.event_sequence))
      << "The shared decision function refuses a delta the broker delivered. "
         "Two opinions about whether a delta applies is how one of them "
         "becomes wrong.";
}

// Acknowledgement is what queue depth is measured from, so a stream that is
// acknowledged keeps delivering. Asserted before the backpressure cases,
// because a stream that stopped for an unrelated reason would make them all
// pass.
IN_PROC_BROWSER_TEST_F(DeltaStreamTest, AnAcknowledgedStreamKeepsDelivering) {
  const ObservationEnvelope observed = ObserveFixture("mutation-race");
  const SubscriptionEnvelope subscription = SubscribeToCurrentPage(observed);
  ASSERT_EQ(ObservationResultCode::kOk, subscription.code);

  for (int round = 0; round < 3; ++round) {
    MutateThePage();
    ASSERT_TRUE(client().WaitForDeltaCount(static_cast<size_t>(round) + 1))
        << "round " << round;
    service()->AcknowledgeDelta(subscription.subscription_id.value(),
                                client().deltas().back().event_sequence);
  }
  EXPECT_GE(client().deltas().size(), 3u);
}

// A cross-document navigation kills the projection, and the notice says the
// epoch is retired. A subscriber that kept applying deltas across a navigation
// would be describing a page that is gone.
IN_PROC_BROWSER_TEST_F(DeltaStreamTest, NavigationInvalidatesTheProjection) {
  const ObservationEnvelope observed = ObserveFixture("mutation-race");
  const SubscriptionEnvelope subscription = SubscribeToCurrentPage(observed);
  ASSERT_EQ(ObservationResultCode::kOk, subscription.code);

  ASSERT_TRUE(NavigateToFixture("static-article"));
  ASSERT_TRUE(client().WaitForInvalidationCount(1));

  bool retired = false;
  for (const InvalidationNotice& notice : client().invalidations()) {
    if (notice.reason == InvalidationCode::kCrossDocumentCommit) {
      retired = true;
      EXPECT_TRUE(notice.retires_page_epoch);
      EXPECT_TRUE(notice.resnapshot_required);
    }
  }
  EXPECT_TRUE(retired);

  // Nothing further may be delivered for that subscription.
  const size_t delivered_before = client().deltas().size();
  MutateThePage();
  EXPECT_EQ(delivered_before, client().deltas().size())
      << "A delta arrived for a subscription whose document is gone.";
}

// Unsubscribing is terminal and idempotent. A second call must not produce a
// second teardown, and no further signal may arrive for that identifier.
IN_PROC_BROWSER_TEST_F(DeltaStreamTest, UnsubscribeIsTerminalAndIdempotent) {
  const ObservationEnvelope observed = ObserveFixture("mutation-race");
  const SubscriptionEnvelope subscription = SubscribeToCurrentPage(observed);
  ASSERT_EQ(ObservationResultCode::kOk, subscription.code);

  service()->Unsubscribe(subscription.subscription_id.value());
  service()->Unsubscribe(subscription.subscription_id.value());

  const size_t delivered_before = client().deltas().size();
  MutateThePage();
  EXPECT_EQ(delivered_before, client().deltas().size());

  EXPECT_TRUE(AuditStreamAssertions::SubscriptionEventOrder(
      audit_stream(), subscription.subscription_id.value().value,
      {SubscriptionEventKind::kOpened, SubscriptionEventKind::kClosed}));
}

}  // namespace
}  // namespace taffy::test

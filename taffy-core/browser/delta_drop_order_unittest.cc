// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/common/public/bip_delta.h"

#include <algorithm>
#include <optional>
#include <vector>

#include "taffy/common/public/bip_subscription.h"
#include "testing/gtest/include/gtest/gtest.h"

// The drop order of protocol section 10, and the type-level guarantee that
// makes it more than a convention.
//
// Most of what matters here is proved at compile time by the static assertions
// in bip_delta.h. These tests cover what a static assertion cannot: that the
// factory really does refuse the protected classes at run time, that the shed
// order is a total order, and that a notice which lost a protected class
// cannot claim the projection survived.

namespace taffy {
namespace {

TEST(DeltaDropOrderTest, ProtectedClassesCannotBecomeSheddable) {
  // The entire point of the type. There is no constructor a caller can reach,
  // and the factory is the only way in.
  EXPECT_FALSE(SheddableDeltaClass::From(DeltaClass::kNodeRemoved).has_value());
  EXPECT_FALSE(SheddableDeltaClass::From(DeltaClass::kLifecycle).has_value());
}

TEST(DeltaDropOrderTest, EverySheddableClassCanBecomeSheddable) {
  for (DeltaClass delta_class : kDeltaShedOrder) {
    EXPECT_TRUE(SheddableDeltaClass::From(delta_class).has_value())
        << "class " << static_cast<int>(delta_class)
        << " is in the shed order but the factory refuses it";
  }
}

TEST(DeltaDropOrderTest, ShedOrderIsTotalAndStartsWithTheOptionalSignals) {
  std::vector<SheddableDeltaClass> classes;
  for (DeltaClass delta_class : kDeltaShedOrder) {
    classes.push_back(*SheddableDeltaClass::From(delta_class));
  }
  // Sorting by the type's own ordering must reproduce the declared order: the
  // ordering and the array are two statements of one policy, and this is what
  // stops them drifting.
  std::vector<SheddableDeltaClass> sorted = classes;
  std::sort(sorted.begin(), sorted.end());
  EXPECT_EQ(classes, sorted);

  EXPECT_EQ(classes.front().value(), DeltaClass::kText);
  EXPECT_LT(classes.front().drop_rank(), classes.back().drop_rank());
}

TEST(DeltaDropOrderTest, TextIsDroppedBeforeAnyStructuralChange) {
  const SheddableDeltaClass text = *SheddableDeltaClass::From(DeltaClass::kText);
  const SheddableDeltaClass added =
      *SheddableDeltaClass::From(DeltaClass::kNodeAdded);
  EXPECT_LT(text.drop_rank(), added.drop_rank());
}

TEST(DeltaDropOrderTest, ANoticeThatLostAProtectedClassMustRequireResnapshot) {
  BackpressureNotice notice;
  notice.subscription_id = SubscriptionId{"sub_1"};
  notice.tab_id = TabId{"tab_1"};
  notice.frame_id = FrameId{"frame_1"};
  notice.action = BackpressureAction::kCoalesced;
  notice.dropped_classes = {DeltaClass::kNodeRemoved};
  notice.resnapshot_required = false;
  EXPECT_FALSE(BackpressureNoticeIsWellFormed(notice));

  notice.resnapshot_required = true;
  EXPECT_TRUE(BackpressureNoticeIsWellFormed(notice));
}

TEST(DeltaDropOrderTest, ScopeReductionMustSayWhatItReducedTo) {
  BackpressureNotice notice;
  notice.subscription_id = SubscriptionId{"sub_1"};
  notice.tab_id = TabId{"tab_1"};
  notice.frame_id = FrameId{"frame_1"};
  notice.action = BackpressureAction::kScopeReduced;
  EXPECT_FALSE(BackpressureNoticeIsWellFormed(notice));

  notice.reduced_scope = ObservationScope::kViewport;
  EXPECT_TRUE(BackpressureNoticeIsWellFormed(notice));
}

TEST(DeltaDropOrderTest, ActionsThatKillTheProjectionMustSaySo) {
  BackpressureNotice notice;
  notice.subscription_id = SubscriptionId{"sub_1"};
  notice.tab_id = TabId{"tab_1"};
  notice.frame_id = FrameId{"frame_1"};
  notice.action = BackpressureAction::kSubscriptionStopped;
  notice.resnapshot_required = false;
  EXPECT_FALSE(BackpressureNoticeIsWellFormed(notice));

  notice.resnapshot_required = true;
  EXPECT_TRUE(BackpressureNoticeIsWellFormed(notice));
}

TEST(DeltaDropOrderTest, ApplicabilityRequiresEpochRevisionAndSequence) {
  DeltaProjectionCursor cursor;
  cursor.page_epoch = PageEpoch{"epoch_1"};
  cursor.revision = 7;
  cursor.event_sequence = 3;

  // The happy case: next sequence, matching from-revision, forward progress.
  EXPECT_EQ(DeltaApplicability(cursor, PageEpoch{"epoch_1"}, 7, 8, 4),
            DeltaRejectReason::kNone);

  // A different document. Checked first, because no revision or sequence
  // reasoning could rescue it.
  EXPECT_EQ(DeltaApplicability(cursor, PageEpoch{"epoch_2"}, 7, 8, 4),
            DeltaRejectReason::kEpochMismatch);

  // Already applied, and one that lost the race with the one that was.
  EXPECT_EQ(DeltaApplicability(cursor, PageEpoch{"epoch_1"}, 7, 8, 3),
            DeltaRejectReason::kDuplicate);
  EXPECT_EQ(DeltaApplicability(cursor, PageEpoch{"epoch_1"}, 7, 8, 2),
            DeltaRejectReason::kOutOfOrder);

  // A skipped sequence number. Stronger than a revision mismatch, and checked
  // before one for that reason.
  EXPECT_EQ(DeltaApplicability(cursor, PageEpoch{"epoch_1"}, 7, 8, 6),
            DeltaRejectReason::kSequenceGap);

  // The right place in the stream, but starting from a revision the
  // subscriber does not hold.
  EXPECT_EQ(DeltaApplicability(cursor, PageEpoch{"epoch_1"}, 6, 8, 4),
            DeltaRejectReason::kRevisionMismatch);

  // A delta that does not move the revision forward could never be applied on
  // top of itself, so it is a mismatch rather than a harmless no-op.
  EXPECT_EQ(DeltaApplicability(cursor, PageEpoch{"epoch_1"}, 7, 7, 4),
            DeltaRejectReason::kRevisionMismatch);
}

TEST(DeltaDropOrderTest, OnlyDuplicatesAndReorderingLeaveTheProjectionAlive) {
  EXPECT_TRUE(ProjectionSurvives(DeltaRejectReason::kNone));
  EXPECT_TRUE(ProjectionSurvives(DeltaRejectReason::kDuplicate));
  EXPECT_TRUE(ProjectionSurvives(DeltaRejectReason::kOutOfOrder));

  for (DeltaRejectReason reason :
       {DeltaRejectReason::kSequenceGap, DeltaRejectReason::kEpochMismatch,
        DeltaRejectReason::kRevisionMismatch,
        DeltaRejectReason::kSubscriptionNotApplying,
        DeltaRejectReason::kOverBudget, DeltaRejectReason::kAdapterRestart,
        DeltaRejectReason::kUnknownField}) {
    EXPECT_FALSE(ProjectionSurvives(reason))
        << "reason " << static_cast<int>(reason)
        << " must force a fresh snapshot";
  }
}

// The other half of the same rule. Killing a projection without naming why
// leaves a subscriber unable to tell a change that was lost from a document
// that is gone, and protocol section 6.3 gives it the right to know which. So
// every reject that ends a projection either carries an invalidation code or
// is the one case that was already reported.
TEST(DeltaDropOrderTest, EveryFatalRejectNamesAnInvalidationOrWasAlreadyNamed) {
  // The three that leave the projection usable have nothing to announce.
  EXPECT_EQ(InvalidationCodeForDeltaReject(DeltaRejectReason::kNone),
            std::nullopt);
  EXPECT_EQ(InvalidationCodeForDeltaReject(DeltaRejectReason::kDuplicate),
            std::nullopt);
  EXPECT_EQ(InvalidationCodeForDeltaReject(DeltaRejectReason::kOutOfOrder),
            std::nullopt);
  // And the one that describes a projection which was already dead when the
  // message arrived: announcing it again would report one death once per
  // refused message for as long as the renderer kept sending.
  EXPECT_EQ(InvalidationCodeForDeltaReject(
                DeltaRejectReason::kSubscriptionNotApplying),
            std::nullopt);

  // The four the contract names for a broker-detected stream failure. Pinned
  // individually, because a subscriber reads the code and a neighbouring one
  // would describe a different failure.
  EXPECT_EQ(InvalidationCodeForDeltaReject(DeltaRejectReason::kSequenceGap),
            InvalidationCode::kSequenceGap);
  EXPECT_EQ(InvalidationCodeForDeltaReject(DeltaRejectReason::kOverBudget),
            InvalidationCode::kDeltaOverflow);
  EXPECT_EQ(InvalidationCodeForDeltaReject(DeltaRejectReason::kUnknownField),
            InvalidationCode::kUnknownDeltaField);
  EXPECT_EQ(InvalidationCodeForDeltaReject(DeltaRejectReason::kAdapterRestart),
            InvalidationCode::kAdapterRestart);

  for (DeltaRejectReason reason :
       {DeltaRejectReason::kSequenceGap, DeltaRejectReason::kEpochMismatch,
        DeltaRejectReason::kRevisionMismatch, DeltaRejectReason::kOverBudget,
        DeltaRejectReason::kAdapterRestart,
        DeltaRejectReason::kUnknownField}) {
    ASSERT_FALSE(ProjectionSurvives(reason));
    const std::optional<InvalidationCode> code =
        InvalidationCodeForDeltaReject(reason);
    ASSERT_TRUE(code.has_value())
        << "reason " << static_cast<int>(reason)
        << " ends the projection without naming why";
    // None of them means the document went away. The stream lost its place
    // inside a page that is still there, so a fresh snapshot of the epoch the
    // subscriber already holds is the way back; retiring the epoch here would
    // force a rebind nothing asked for.
    EXPECT_FALSE(InvalidationRetiresDocument(*code))
        << "reason " << static_cast<int>(reason)
        << " retired a document that never went anywhere";
  }
}

}  // namespace
}  // namespace taffy

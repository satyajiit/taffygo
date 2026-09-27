// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/components/intelligence/content/observability_recorder.h"

#include <string>
#include <vector>

#include "testing/gtest/include/gtest/gtest.h"

// Protocol section 16 in test form: a record carries identifiers, enums and
// counts, and nothing that could be page content.

namespace taffy {
namespace {

class CapturingSink : public ObservabilitySink {
 public:
  void RecordObservation(const ObservationRecord& record) override {
    observations.push_back(record);
  }
  void RecordAction(const ActionRecord& record) override {
    actions.push_back(record);
  }
  void RecordSubscription(const SubscriptionRecord& record) override {
    subscriptions.push_back(record);
  }

  std::vector<ObservationRecord> observations;
  std::vector<ActionRecord> actions;
  std::vector<SubscriptionRecord> subscriptions;
};

TEST(ObservabilityRecorderTest, RecordsAreTriviallyCopyable) {
  // The same assertion the header makes at compile time, restated here so that
  // a reader of the tests learns the rule too.
  static_assert(std::is_trivially_copyable_v<ObservationRecord>);
  static_assert(std::is_trivially_copyable_v<ActionRecord>);
  // The delta-stream record is held to the same rule. It is the one most
  // tempting to loosen, because a list of the node identifiers a delta retired
  // would be genuinely useful for debugging — and would be exactly the
  // persistent cross-site identifier protocol section 16 forbids.
  static_assert(std::is_trivially_copyable_v<SubscriptionRecord>);
  SUCCEED();
}

TEST(ObservabilityRecorderTest, IdentifiersAreBoundedAndNulTerminated) {
  const std::string overlong(kMaxIdentifierChars * 2, 'x');
  const RecordIdentifier id = ToRecordIdentifier(overlong);
  EXPECT_EQ(kMaxIdentifierChars, std::string(id.chars.data()).size());
  EXPECT_EQ('\0', id.chars[kMaxIdentifierChars]);
}

TEST(ObservabilityRecorderTest, NothingIsBufferedWithoutASink) {
  ObservabilityRecorder recorder;
  ActionRecord record;
  recorder.Record(record);  // No sink: dropped, not queued.

  CapturingSink sink;
  recorder.SetSink(&sink);
  EXPECT_TRUE(sink.actions.empty());
}

TEST(ObservabilityRecorderTest, RecordsReachTheSink) {
  CapturingSink sink;
  ObservabilityRecorder recorder;
  recorder.SetSink(&sink);

  ActionRecord record;
  record.request_id = ToRecordIdentifier("req_1");
  record.code = ActionResultCode::kVerified;
  recorder.Record(record);

  ASSERT_EQ(1u, sink.actions.size());
  EXPECT_EQ(ActionResultCode::kVerified, sink.actions[0].code);
  EXPECT_EQ("req_1", std::string(sink.actions[0].request_id.chars.data()));
}

TEST(ObservabilityRecorderTest, DirectObservationAuditIsTasklessAndTyped) {
  const ObservationAuthoritySubject subject =
      ObservationAuthoritySubject::ForDirectUserIntent(
          DirectIntentId{"direct-intent-inspector-1"});
  ObservationRecord record;

  ASSERT_TRUE(PopulateObservationRecordAuthority(subject, &record));
  EXPECT_EQ(ObservationRecordAuthoritySubjectKind::kDirectUserIntent,
            record.authority_subject_kind);
  EXPECT_EQ("direct-intent-inspector-1",
            std::string(record.authority_subject_id.chars.data()));

  ObservationAuthoritySubject forged_task =
      ObservationAuthoritySubject::ForTask(
          TaskId{"direct-intent-inspector-1"});
  EXPECT_FALSE(PopulateObservationRecordAuthority(forged_task, &record));
  ObservationAuthoritySubject forged_direct =
      ObservationAuthoritySubject::ForDirectUserIntent(
          DirectIntentId{"task-1"});
  EXPECT_FALSE(PopulateObservationRecordAuthority(forged_direct, &record));
}

TEST(ObservabilityRecorderTest, StaleReasonsAreCategorized) {
  EXPECT_EQ(StaleReason::kPageEpoch, ObservabilityRecorder::StaleReasonFor(
                                         ActionResultCode::kStalePageEpoch));
  EXPECT_EQ(StaleReason::kGraphRevision,
            ObservabilityRecorder::StaleReasonFor(ActionResultCode::kStaleGraph));
  EXPECT_EQ(StaleReason::kOrigin, ObservabilityRecorder::StaleReasonFor(
                                      ActionResultCode::kOriginChanged));
  // A verified action is not stale, and neither is a policy refusal.
  EXPECT_EQ(StaleReason::kNone,
            ObservabilityRecorder::StaleReasonFor(ActionResultCode::kVerified));
  EXPECT_EQ(StaleReason::kNone, ObservabilityRecorder::StaleReasonFor(
                                    ActionResultCode::kDeniedByPolicy));
}

TEST(ObservabilityRecorderTest, OpaqueOriginsAreCategorizedAsOpaque) {
  EXPECT_EQ(OriginCategory::kOpaque,
            ObservabilityRecorder::CategorizeOrigin(
                /*same_origin_as_tab=*/true, /*same_site_as_tab=*/true,
                /*is_opaque=*/true));
}

}  // namespace
}  // namespace taffy

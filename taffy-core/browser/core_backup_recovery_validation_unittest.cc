// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/core_backup_recovery_validation.h"

#include <cstdint>
#include <utility>
#include <vector>

#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

namespace core_mojom = core_service::mojom;

constexpr uint64_t kGeneration = 11u;
constexpr uint64_t kNow = 100u;
constexpr char kOwner[] = "11111111-1111-4111-8111-111111111111";

core_mojom::OperationEnvelopePtr Operation() {
  return core_mojom::OperationEnvelope::New("inspect-recovery", kGeneration, 0u,
                                            kNow + 1u, "inspect-recovery-once");
}

std::vector<uint8_t> Digest(uint8_t value) {
  return std::vector<uint8_t>(32u, value);
}

core_mojom::BackupRestoreRecoveryBindingPtr Binding() {
  auto binding = core_mojom::BackupRestoreRecoveryBinding::New();
  binding->reservation_id = "reservation-1";
  binding->owner_profile_id = kOwner;
  binding->target_kind =
      core_mojom::BackupRestoreTargetKind::kNewRegularProfile;
  binding->target_profile_id = "22222222-2222-4222-8222-222222222222";
  binding->backup_id = "backup-1";
  binding->snapshot_sha256 = Digest(1u);
  binding->confirmation_sha256 = Digest(2u);
  binding->selection = {core_mojom::BackupRecordKind::kLibraryEntry};
  binding->record_count = 1u;
  binding->candidate_records_sha256 = Digest(3u);
  return binding;
}

core_mojom::BackupRestoreRecoveryRecordPtr IntentRecord() {
  auto record = core_mojom::BackupRestoreRecoveryRecord::New();
  record->format_version = 1u;
  record->sequence = 1u;
  record->binding = Binding();
  record->fact_kind =
      core_mojom::BackupRestoreRecoveryFactKind::kIntentRecorded;
  record->intent = core_mojom::BackupRestoreRecoveryIntentFact::New(
      "commit-1", core_mojom::BackupRestorePhysicalIntent::kCommitCandidate);
  return record;
}

core_mojom::BackupRestoreRecoveryInspectionRequestPtr Request() {
  auto request = core_mojom::BackupRestoreRecoveryInspectionRequest::New();
  request->operation = Operation();
  request->records.push_back(IntentRecord());
  return request;
}

bool ValidRequest(
    const core_mojom::BackupRestoreRecoveryInspectionRequest& request) {
  return IsValidBackupRestoreRecoveryInspectionRequest(request, kGeneration,
                                                       kNow, kOwner);
}

core_mojom::BackupRestoreRecoveryInspectionResultPtr ReconcileResult() {
  auto result = core_mojom::BackupRestoreRecoveryInspectionResult::New();
  result->operation = Operation();
  result->status =
      core_mojom::BackupRestoreRecoveryInspectionStatus::kSucceeded;
  result->classification = core_mojom::BackupRestoreRecoveryClassification::New(
      core_mojom::BackupRestoreRecoveryClassificationKind::kReconcileRequired,
      core_mojom::BackupRestoreRecoveryReconciliation::New(
          "commit-1",
          core_mojom::BackupRestorePhysicalIntent::kCommitCandidate));
  return result;
}

TEST(CoreBackupRecoveryValidationTest,
     RequestNeedsLiveOperationCurrentOwnerAndBoundedHistory) {
  EXPECT_TRUE(ValidRequest(*Request()));

  auto changed = Request();
  changed->operation->service_generation++;
  EXPECT_FALSE(ValidRequest(*changed));
  changed = Request();
  changed->operation->deadline_monotonic_ms = kNow;
  EXPECT_FALSE(ValidRequest(*changed));
  changed = Request();
  changed->records.front()->binding->owner_profile_id = "another-source";
  EXPECT_FALSE(ValidRequest(*changed));

  changed = Request();
  const auto row = changed->records.front().Clone();
  while (changed->records.size() <=
         core_mojom::kMaxBackupRestoreRecoveryRecords) {
    changed->records.push_back(row.Clone());
  }
  EXPECT_FALSE(ValidRequest(*changed));
}

TEST(CoreBackupRecoveryValidationTest,
     RequestRejectsNullsWidthsAndUnknownTransportEnums) {
  for (int mutation = 0; mutation < 12; ++mutation) {
    SCOPED_TRACE(mutation);
    auto changed = Request();
    auto& record = changed->records.front();
    switch (mutation) {
      case 0:
        changed->operation.reset();
        break;
      case 1:
        record.reset();
        break;
      case 2:
        record->binding.reset();
        break;
      case 3:
        record->binding->snapshot_sha256.resize(31u);
        break;
      case 4:
        record->binding->confirmation_sha256.resize(33u);
        break;
      case 5:
        record->binding->candidate_records_sha256.clear();
        break;
      case 6:
        record->binding->selection = {
            static_cast<core_mojom::BackupRecordKind>(255)};
        break;
      case 7:
        record->binding->target_kind =
            static_cast<core_mojom::BackupRestoreTargetKind>(255);
        break;
      case 8:
        record->fact_kind =
            static_cast<core_mojom::BackupRestoreRecoveryFactKind>(255);
        break;
      case 9:
        record->intent->intent =
            static_cast<core_mojom::BackupRestorePhysicalIntent>(255);
        break;
      case 10:
        record->outcome = core_mojom::BackupRestoreRecoveryOutcomeFact::New(
            "commit-1", core_mojom::BackupRestoreObservedOutcome::kCompleted);
        break;
      case 11:
        record->intent->intent_id.assign(core_mojom::kMaxBackupIdBytes + 1u,
                                         'x');
        break;
    }
    EXPECT_FALSE(ValidRequest(*changed));
  }
}

// The wire rules stop where the wire's own knowledge stops. A sequence that
// skips and a format version this browser has never heard of are questions
// about what happened, and neither is read here: the portable reducer answers
// them and names one of the recovery errors for them, which it cannot do for a
// request refused before it arrives. An empty history is the same kind of
// answer — nothing recorded — rather than a malformed one.
TEST(CoreBackupRecoveryValidationTest,
     SemanticHistoryQuestionsRemainForPortableReducer) {
  auto request = Request();
  request->records.front()->sequence = 99u;
  request->records.front()->format_version = 91u;
  EXPECT_TRUE(ValidRequest(*request));

  request->records.clear();
  EXPECT_TRUE(ValidRequest(*request));
}

// The other side of that boundary, kept beside it because the two are only
// meaningful together. Each of these is a binding the wire itself can see is
// malformed, so none of them is a question for the reducer.
TEST(CoreBackupRecoveryValidationTest,
     MalformedBindingsAreRefusedBeforeTheReducerIsAsked) {
  auto no_reservation = Request();
  no_reservation->records.front()->binding->reservation_id.clear();
  EXPECT_FALSE(ValidRequest(*no_reservation));

  // The right width and no content. A digest of nothing identifies nothing.
  auto zero_candidate_digest = Request();
  zero_candidate_digest->records.front()
      ->binding->candidate_records_sha256.assign(32u, 0u);
  EXPECT_FALSE(ValidRequest(*zero_candidate_digest));

  // The selection is canonical, so it carries each kind once and in order.
  auto unordered_selection = Request();
  unordered_selection->records.front()->binding->selection = {
      core_mojom::BackupRecordKind::kMemoryRecord,
      core_mojom::BackupRecordKind::kLibraryEntry};
  EXPECT_FALSE(ValidRequest(*unordered_selection));

  // Two kinds selected and no records to carry them cannot both be true.
  auto countless_selection = Request();
  countless_selection->records.front()->binding->selection = {
      core_mojom::BackupRecordKind::kLibraryEntry,
      core_mojom::BackupRecordKind::kMemoryRecord};
  countless_selection->records.front()->binding->record_count = 0u;
  EXPECT_FALSE(ValidRequest(*countless_selection));
}

TEST(CoreBackupRecoveryValidationTest,
     ResultRequiresExactEchoAndClosedConditionalBodies) {
  auto expected = Operation();
  EXPECT_TRUE(IsValidBackupRestoreRecoveryInspectionResult(*expected,
                                                           *ReconcileResult()));

  for (int mutation = 0; mutation < 8; ++mutation) {
    SCOPED_TRACE(mutation);
    auto changed = ReconcileResult();
    switch (mutation) {
      case 0:
        changed->operation->idempotency_key += "-drift";
        break;
      case 1:
        changed->status =
            static_cast<core_mojom::BackupRestoreRecoveryInspectionStatus>(255);
        break;
      case 2:
        changed->classification.reset();
        break;
      case 3:
        changed->failure = core_mojom::BackupRestoreRecoveryFailure::New(
            core_mojom::BackupRestoreRecoveryError::kInvalidBinding);
        break;
      case 4:
        changed->classification->kind =
            static_cast<core_mojom::BackupRestoreRecoveryClassificationKind>(
                255);
        break;
      case 5:
        changed->classification->reconciliation.reset();
        break;
      case 6:
        changed->classification->reconciliation->intent =
            static_cast<core_mojom::BackupRestorePhysicalIntent>(255);
        break;
      case 7:
        changed->classification->reconciliation->intent_id = "bad\nidentity";
        break;
    }
    EXPECT_FALSE(
        IsValidBackupRestoreRecoveryInspectionResult(*expected, *changed));
  }
}

TEST(CoreBackupRecoveryValidationTest,
     NonSuccessShapesNeverCarryAClassificationOrAuthorityLikeBody) {
  auto expected = Operation();
  auto invalid = core_mojom::BackupRestoreRecoveryInspectionResult::New(
      expected.Clone(),
      core_mojom::BackupRestoreRecoveryInspectionStatus::kInvalidHistory,
      nullptr,
      core_mojom::BackupRestoreRecoveryFailure::New(
          core_mojom::BackupRestoreRecoveryError::kInvalidSequence));
  EXPECT_TRUE(
      IsValidBackupRestoreRecoveryInspectionResult(*expected, *invalid));
  invalid->failure->error =
      static_cast<core_mojom::BackupRestoreRecoveryError>(255);
  EXPECT_FALSE(
      IsValidBackupRestoreRecoveryInspectionResult(*expected, *invalid));

  auto unavailable = core_mojom::BackupRestoreRecoveryInspectionResult::New(
      expected.Clone(),
      core_mojom::BackupRestoreRecoveryInspectionStatus::kUnavailable, nullptr,
      nullptr);
  EXPECT_TRUE(
      IsValidBackupRestoreRecoveryInspectionResult(*expected, *unavailable));
  unavailable
      ->classification = core_mojom::BackupRestoreRecoveryClassification::New(
      core_mojom::BackupRestoreRecoveryClassificationKind::kRollbackAvailable,
      nullptr);
  EXPECT_FALSE(
      IsValidBackupRestoreRecoveryInspectionResult(*expected, *unavailable));
}

}  // namespace
}  // namespace taffy

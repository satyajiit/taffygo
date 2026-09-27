// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"
#include "taffy/services/core/rust_core_backup.h"
#include "taffy/services/core/service_bridge_backup_ffi.rs.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

namespace bridge = core_bridge;
namespace core_mojom = core_service::mojom;
namespace internal = core_service_internal;

constexpr uint64_t kGeneration = 17u;

core_mojom::OperationEnvelopePtr Operation() {
  return core_mojom::OperationEnvelope::New("inspect-recovery", kGeneration, 0u,
                                            8'000u, "inspect-recovery-once");
}

core_mojom::BackupRestoreRecoveryBindingPtr Binding() {
  auto binding = core_mojom::BackupRestoreRecoveryBinding::New();
  binding->reservation_id = "reservation-1";
  binding->owner_profile_id = "source-profile";
  binding->target_kind =
      core_mojom::BackupRestoreTargetKind::kNewRegularProfile;
  binding->target_profile_id = "target-profile";
  binding->backup_id = "backup-1";
  binding->snapshot_sha256.assign(32u, 0x11u);
  binding->confirmation_sha256.assign(32u, 0x22u);
  binding->selection = {core_mojom::BackupRecordKind::kLibraryEntry};
  binding->record_count = 1u;
  binding->candidate_records_sha256.assign(32u, 0x33u);
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

bridge::BridgeBackupOperation BridgeOperation() {
  bridge::BridgeBackupOperation operation;
  operation.operation_id = "inspect-recovery";
  operation.service_generation = kGeneration;
  operation.task_revision = 0u;
  operation.deadline_monotonic_ms = 8'000u;
  operation.idempotency_key = "inspect-recovery-once";
  return operation;
}

bridge::BridgeBackupRestoreRecoveryInspectionResult BridgeResult(
    core_mojom::BackupRestoreRecoveryInspectionStatus status) {
  bridge::BridgeBackupRestoreRecoveryInspectionResult result;
  result.operation = BridgeOperation();
  result.status = static_cast<uint8_t>(status);
  result.classification.kind = static_cast<uint8_t>(
      core_mojom::BackupRestoreRecoveryClassificationKind::kRollbackAvailable);
  result.classification.reconciliation.intent_id = "commit-1";
  result.classification.reconciliation.intent = static_cast<uint8_t>(
      core_mojom::BackupRestorePhysicalIntent::kCommitCandidate);
  result.failure.error = static_cast<uint8_t>(
      core_mojom::BackupRestoreRecoveryError::kInvalidBinding);
  return result;
}

TEST(RustCoreBackupRecoveryTest, ValidInputPreservesAllContentFreeFacts) {
  auto request = Request();
  const auto projected =
      internal::ToBridgeBackupRestoreRecoveryInspectionRequest(*request);
  ASSERT_TRUE(projected);
  EXPECT_EQ("inspect-recovery", std::string(projected->operation.operation_id));
  ASSERT_EQ(1u, projected->records.size());
  const auto& record = projected->records.front();
  EXPECT_EQ(1u, record.format_version);
  EXPECT_EQ(1u, record.sequence);
  EXPECT_TRUE(record.has_intent);
  EXPECT_FALSE(record.has_outcome);
  EXPECT_EQ("reservation-1", std::string(record.binding.reservation_id));
  EXPECT_EQ("source-profile", std::string(record.binding.owner_profile_id));
  EXPECT_EQ(1u, record.binding.record_count);
  ASSERT_EQ(1u, record.binding.selection.size());
  EXPECT_EQ(static_cast<uint8_t>(core_mojom::BackupRecordKind::kLibraryEntry),
            record.binding.selection.front());
  EXPECT_EQ(0x33u, record.binding.candidate_records_sha256.front());
  EXPECT_EQ("commit-1", std::string(record.intent.intent_id));
}

TEST(RustCoreBackupRecoveryTest,
     InputRejectsMissingPointersWrongWidthsAndExcessRows) {
  for (int mutation = 0; mutation < 7; ++mutation) {
    SCOPED_TRACE(mutation);
    auto changed = Request();
    switch (mutation) {
      case 0:
        changed->operation.reset();
        break;
      case 1:
        changed->records.front().reset();
        break;
      case 2:
        changed->records.front()->binding.reset();
        break;
      case 3:
        changed->records.front()->binding->snapshot_sha256.resize(31u);
        break;
      case 4:
        changed->records.front()->binding->confirmation_sha256.resize(33u);
        break;
      case 5:
        changed->records.front()->binding->candidate_records_sha256.clear();
        break;
      case 6:
        changed->records.front()->binding->selection.assign(
            core_mojom::kMaxBackupSelectionKinds + 1u,
            core_mojom::BackupRecordKind::kLibraryEntry);
        break;
    }
    EXPECT_FALSE(
        internal::ToBridgeBackupRestoreRecoveryInspectionRequest(*changed));
  }

  auto oversized = Request();
  const auto row = oversized->records.front().Clone();
  while (oversized->records.size() <=
         core_mojom::kMaxBackupRestoreRecoveryRecords) {
    oversized->records.push_back(row.Clone());
  }
  EXPECT_FALSE(
      internal::ToBridgeBackupRestoreRecoveryInspectionRequest(*oversized));
}

TEST(RustCoreBackupRecoveryTest,
     InputRejectsUnknownEnumsAndPreservesPresenceBitsForRustValidation) {
  for (int mutation = 0; mutation < 6; ++mutation) {
    SCOPED_TRACE(mutation);
    auto changed = Request();
    auto& record = changed->records.front();
    switch (mutation) {
      case 0:
        record->binding->target_kind =
            static_cast<core_mojom::BackupRestoreTargetKind>(255);
        break;
      case 1:
        record->binding->selection.front() =
            static_cast<core_mojom::BackupRecordKind>(255);
        break;
      case 2:
        record->fact_kind =
            static_cast<core_mojom::BackupRestoreRecoveryFactKind>(255);
        break;
      case 3:
        record->intent->intent =
            static_cast<core_mojom::BackupRestorePhysicalIntent>(255);
        break;
      case 4:
        record->intent.reset();
        record->outcome = core_mojom::BackupRestoreRecoveryOutcomeFact::New(
            "commit-1",
            static_cast<core_mojom::BackupRestoreObservedOutcome>(255));
        break;
      case 5:
        record->outcome = core_mojom::BackupRestoreRecoveryOutcomeFact::New(
            "commit-1",
            core_mojom::BackupRestoreObservedOutcome::kOutcomeUnknown);
        break;
    }
    const auto projected =
        internal::ToBridgeBackupRestoreRecoveryInspectionRequest(*changed);
    if (mutation == 5) {
      ASSERT_TRUE(projected);
      EXPECT_TRUE(projected->records.front().has_intent);
      EXPECT_TRUE(projected->records.front().has_outcome);
    } else {
      EXPECT_FALSE(projected);
    }
  }
}

TEST(RustCoreBackupRecoveryTest,
     SuccessfulOutputHasOneClosedObservationalClassification) {
  for (uint8_t kind = 0u; kind <= 4u; ++kind) {
    SCOPED_TRACE(kind);
    auto input = BridgeResult(
        core_mojom::BackupRestoreRecoveryInspectionStatus::kSucceeded);
    input.has_classification = true;
    input.classification.kind = kind;
    input.classification.has_reconciliation = kind == 0u;
    auto output =
        internal::ToMojoBackupRestoreRecoveryInspectionResult(std::move(input));
    ASSERT_TRUE(output);
    ASSERT_TRUE(output->classification);
    EXPECT_EQ(kind, static_cast<uint8_t>(output->classification->kind));
    EXPECT_EQ(kind == 0u,
              static_cast<bool>(output->classification->reconciliation));
    EXPECT_FALSE(output->failure);
  }
}

TEST(RustCoreBackupRecoveryTest,
     InvalidHistoryOutputPreservesEveryClosedPortableError) {
  for (uint8_t error = 0u; error <= 13u; ++error) {
    SCOPED_TRACE(error);
    auto input = BridgeResult(
        core_mojom::BackupRestoreRecoveryInspectionStatus::kInvalidHistory);
    input.has_failure = true;
    input.failure.error = error;
    auto output =
        internal::ToMojoBackupRestoreRecoveryInspectionResult(std::move(input));
    ASSERT_TRUE(output);
    ASSERT_TRUE(output->failure);
    EXPECT_EQ(error, static_cast<uint8_t>(output->failure->error));
    EXPECT_FALSE(output->classification);
  }
}

TEST(RustCoreBackupRecoveryTest,
     OutputRejectsPresenceDriftUnknownEnumsAndMalformedEcho) {
  for (int mutation = 0; mutation < 8; ++mutation) {
    SCOPED_TRACE(mutation);
    auto input = BridgeResult(
        core_mojom::BackupRestoreRecoveryInspectionStatus::kSucceeded);
    input.has_classification = true;
    input.classification.kind = static_cast<uint8_t>(
        core_mojom::BackupRestoreRecoveryClassificationKind::
            kReconcileRequired);
    input.classification.has_reconciliation = true;
    switch (mutation) {
      case 0:
        input.status = 255u;
        break;
      case 1:
        input.has_classification = false;
        break;
      case 2:
        input.has_failure = true;
        break;
      case 3:
        input.classification.kind = 255u;
        break;
      case 4:
        input.classification.has_reconciliation = false;
        break;
      case 5:
        input.classification.reconciliation.intent = 255u;
        break;
      case 6:
        input.failure.error = 255u;
        break;
      case 7:
        input.operation.operation_id = "";
        break;
    }
    EXPECT_FALSE(internal::ToMojoBackupRestoreRecoveryInspectionResult(
        std::move(input)));
  }
}

TEST(RustCoreBackupRecoveryTest, FailureStatusesCarryNoObservationBodies) {
  for (auto status : {
           core_mojom::BackupRestoreRecoveryInspectionStatus::kInvalidOperation,
           core_mojom::BackupRestoreRecoveryInspectionStatus::kInvalidRecord,
           core_mojom::BackupRestoreRecoveryInspectionStatus::kUnavailable,
       }) {
    auto input = BridgeResult(status);
    auto output =
        internal::ToMojoBackupRestoreRecoveryInspectionResult(std::move(input));
    ASSERT_TRUE(output);
    EXPECT_FALSE(output->classification);
    EXPECT_FALSE(output->failure);
  }
}

}  // namespace
}  // namespace taffy

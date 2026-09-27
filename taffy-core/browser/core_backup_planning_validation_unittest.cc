// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/core_backup_planning_validation.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

namespace mojom = core_service::mojom;

constexpr uint64_t kGeneration = 7u;
constexpr uint64_t kNow = 10'000u;
constexpr char kOwnerProfileId[] = "profile-1";

mojom::OperationEnvelopePtr Operation(std::string id = "backup-operation") {
  return mojom::OperationEnvelope::New(std::move(id), kGeneration, 0u,
                                       kNow + 30'000u, "backup-idempotency");
}

std::vector<uint8_t> Digest(uint8_t value = 1u) {
  return std::vector<uint8_t>(32u, value);
}

mojom::BackupRecordDescriptorPtr Record(mojom::BackupRecordKind kind,
                                        std::string stable_id,
                                        uint64_t bytes = 4u) {
  auto record = mojom::BackupRecordDescriptor::New();
  record->kind = kind;
  record->stable_id = std::move(stable_id);
  record->revision = 1u;
  record->schema_version = 1u;
  record->state = mojom::BackupRecordState::kActive;
  record->plaintext_bytes = bytes;
  record->plaintext_sha256 = Digest();
  return record;
}

mojom::BackupManifestPrepareRequestPtr PrepareRequest() {
  auto request = mojom::BackupManifestPrepareRequest::New();
  request->operation = Operation();
  request->backup_id = "backup-1";
  request->source_installation_id = "installation-1";
  request->created_at_utc = "2026-09-05T00:00:00Z";
  request->selection = {mojom::BackupRecordKind::kLibraryEntry,
                        mojom::BackupRecordKind::kMemoryRecord};
  request->records.push_back(
      Record(mojom::BackupRecordKind::kMemoryRecord, "memory-1"));
  request->records.push_back(
      Record(mojom::BackupRecordKind::kLibraryEntry, "library-1"));
  return request;
}

mojom::BackupManifestPrepareResultPtr PrepareResult(
    const mojom::OperationEnvelope& operation) {
  auto result = mojom::BackupManifestPrepareResult::New();
  result->operation = operation.Clone();
  result->status = mojom::BackupPlanningStatus::kSucceeded;
  result->manifest_plaintext = {1u, 2u, 3u};
  result->snapshot_sha256 = Digest();
  result->payload_plaintext_bytes = 8u;
  result->source_order = {1u, 0u};
  result->expected_sealed_chunks = 2u;
  return result;
}

mojom::BackupManifestInspectResultPtr InspectResult(
    const mojom::OperationEnvelope& operation) {
  auto result = mojom::BackupManifestInspectResult::New();
  result->operation = operation.Clone();
  result->status = mojom::BackupPlanningStatus::kSucceeded;
  result->backup_id = "backup-1";
  result->source_installation_id = "installation-1";
  result->created_at_utc = "2026-09-05T00:00:00Z";
  result->selection = {mojom::BackupRecordKind::kLibraryEntry,
                       mojom::BackupRecordKind::kMemoryRecord};
  result->record_count = 2u;
  result->snapshot_sha256 = Digest();
  result->payload_plaintext_bytes = 4u;
  result->records.push_back(mojom::BackupPayloadLayoutEntry::New(
      mojom::BackupRecordState::kActive, 4u));
  result->records.push_back(mojom::BackupPayloadLayoutEntry::New(
      mojom::BackupRecordState::kTombstone, 0u));
  return result;
}

mojom::BackupRestorePlanResultPtr RestoreResult(
    const mojom::OperationEnvelope& operation,
    const mojom::BackupRestoreTarget& target) {
  auto result = mojom::BackupRestorePlanResult::New();
  result->operation = operation.Clone();
  result->status = mojom::BackupPlanningStatus::kSucceeded;
  result->backup_id = "backup-1";
  result->snapshot_sha256 = Digest();
  result->target = target.Clone();
  result->entries.push_back(mojom::BackupRestorePlanEntry::New(
      mojom::BackupRecordKind::kLibraryEntry, "library-1", 1u,
      mojom::BackupRestoreAction::kStageCreate, 1u,
      mojom::BackupRecordState::kActive, 4u, Digest()));
  result->entries.push_back(mojom::BackupRestorePlanEntry::New(
      mojom::BackupRecordKind::kMemoryRecord, "memory-1", 1u,
      mojom::BackupRestoreAction::kStageDeletion, 1u,
      mojom::BackupRecordState::kTombstone, 0u, std::vector<uint8_t>(32u, 0u)));
  result->has_conflicts = false;
  result->confirmation_sha256 = Digest(2u);
  result->binding = mojom::BackupRestoreBinding::New(
      operation.Clone(), kOwnerProfileId, target.Clone(), result->backup_id,
      result->snapshot_sha256, result->confirmation_sha256);
  return result;
}

std::vector<mojom::StagedBackupRecordPtr> StagedRecords() {
  std::vector<mojom::StagedBackupRecordPtr> staged;
  staged.push_back(mojom::StagedBackupRecord::New(4u, Digest()));
  staged.push_back(
      mojom::StagedBackupRecord::New(0u, std::vector<uint8_t>(32u, 0u)));
  return staged;
}

TEST(CoreBackupPlanningValidationTest,
     OperationMustBeLiveProfileWorkAndEchoExactly) {
  mojom::OperationEnvelopePtr operation = Operation();
  EXPECT_TRUE(IsLiveBackupOperation(operation.get(), kGeneration, kNow));
  EXPECT_TRUE(IsExactBackupOperation(operation.get(), operation.get()));

  mojom::OperationEnvelopePtr changed = operation.Clone();
  changed->task_revision = 1u;
  EXPECT_FALSE(IsLiveBackupOperation(changed.get(), kGeneration, kNow));
  EXPECT_FALSE(IsExactBackupOperation(operation.get(), changed.get()));
  changed = operation.Clone();
  changed->deadline_monotonic_ms = kNow;
  EXPECT_FALSE(IsLiveBackupOperation(changed.get(), kGeneration, kNow));
  changed = operation.Clone();
  changed->service_generation++;
  EXPECT_FALSE(IsLiveBackupOperation(changed.get(), kGeneration, kNow));
  changed = operation.Clone();
  changed->service_generation = 0u;
  EXPECT_FALSE(IsLiveBackupOperation(changed.get(), 0u, kNow));
  changed = operation.Clone();
  changed->operation_id = std::string("invalid-") + static_cast<char>(0xc3) +
                          static_cast<char>(0x28);
  EXPECT_FALSE(IsLiveBackupOperation(changed.get(), kGeneration, kNow));
}

TEST(CoreBackupPlanningValidationTest,
     PrepareRequestRefusesDuplicateIdentityAndAggregateOverflow) {
  mojom::BackupManifestPrepareRequestPtr request = PrepareRequest();
  ASSERT_TRUE(IsValidBackupPrepareRequest(*request, kGeneration, kNow));

  request->records.push_back(request->records.front().Clone());
  EXPECT_FALSE(IsValidBackupPrepareRequest(*request, kGeneration, kNow));
  request = PrepareRequest();
  request->selection.push_back(request->selection.front());
  EXPECT_FALSE(IsValidBackupPrepareRequest(*request, kGeneration, kNow));

  request = PrepareRequest();
  request->records.clear();
  for (size_t index = 0; index < 129u; ++index) {
    request->records.push_back(Record(mojom::BackupRecordKind::kLibraryEntry,
                                      std::to_string(index),
                                      mojom::kMaxBackupRecordBytes));
  }
  EXPECT_FALSE(IsValidBackupPrepareRequest(*request, kGeneration, kNow));
}

TEST(CoreBackupPlanningValidationTest,
     PrepareResultRequiresExactPermutationAndFailureShape) {
  mojom::OperationEnvelopePtr operation = Operation();
  mojom::BackupManifestPrepareResultPtr result = PrepareResult(*operation);
  ASSERT_TRUE(IsValidBackupPrepareResult(*operation, 2u, *result));

  result->source_order = {0u, 0u};
  EXPECT_FALSE(IsValidBackupPrepareResult(*operation, 2u, *result));
  result = PrepareResult(*operation);
  result->operation->idempotency_key = "spliced";
  EXPECT_FALSE(IsValidBackupPrepareResult(*operation, 2u, *result));
  result = PrepareResult(*operation);
  result->expected_sealed_chunks = 1u;
  EXPECT_FALSE(IsValidBackupPrepareResult(*operation, 2u, *result));

  result = PrepareResult(*operation);
  result->status = mojom::BackupPlanningStatus::kInvalidRequest;
  EXPECT_FALSE(IsValidBackupPrepareResult(*operation, 2u, *result));
  result->manifest_plaintext.clear();
  result->snapshot_sha256 = {};
  result->payload_plaintext_bytes = 0u;
  result->source_order.clear();
  result->expected_sealed_chunks = 0u;
  EXPECT_TRUE(IsValidBackupPrepareResult(*operation, 2u, *result));
}

TEST(CoreBackupPlanningValidationTest,
     InspectResultRequiresCanonicalSelectionAndExactAggregate) {
  mojom::OperationEnvelopePtr operation = Operation();
  mojom::BackupManifestInspectResultPtr result = InspectResult(*operation);
  ASSERT_TRUE(IsValidBackupInspectResult(*operation, *result));

  std::swap(result->selection.front(), result->selection.back());
  EXPECT_FALSE(IsValidBackupInspectResult(*operation, *result));
  result = InspectResult(*operation);
  result->payload_plaintext_bytes++;
  EXPECT_FALSE(IsValidBackupInspectResult(*operation, *result));
  result = InspectResult(*operation);
  result->records.back()->plaintext_bytes = 1u;
  EXPECT_FALSE(IsValidBackupInspectResult(*operation, *result));
  result = InspectResult(*operation);
  result->status = mojom::BackupPlanningStatus::kInvalidManifest;
  EXPECT_FALSE(IsValidBackupInspectResult(*operation, *result));
}

TEST(CoreBackupPlanningValidationTest,
     RestoreResultRefusesSplicingDuplicatesAndConflictDrift) {
  mojom::OperationEnvelopePtr operation = Operation();
  auto target = mojom::BackupRestoreTarget::New(
      mojom::BackupRestoreTargetKind::kNewRegularProfile, "profile-2");
  const std::vector<mojom::StagedBackupRecordPtr> staged = StagedRecords();
  mojom::BackupRestorePlanResultPtr result = RestoreResult(*operation, *target);
  ASSERT_TRUE(IsValidBackupRestoreResult(*operation, kOwnerProfileId, *target,
                                         staged, *result));

  result->target->profile_id = "another-profile";
  EXPECT_FALSE(IsValidBackupRestoreResult(*operation, kOwnerProfileId, *target,
                                          staged, *result));
  result = RestoreResult(*operation, *target);
  result->entries.back()->kind = result->entries.front()->kind;
  result->entries.back()->stable_id = result->entries.front()->stable_id;
  EXPECT_FALSE(IsValidBackupRestoreResult(*operation, kOwnerProfileId, *target,
                                          staged, *result));
  result = RestoreResult(*operation, *target);
  result->has_conflicts = true;
  EXPECT_FALSE(IsValidBackupRestoreResult(*operation, kOwnerProfileId, *target,
                                          staged, *result));
  result = RestoreResult(*operation, *target);
  result->entries.front()->schema_version = 0u;
  EXPECT_FALSE(IsValidBackupRestoreResult(*operation, kOwnerProfileId, *target,
                                          staged, *result));
  result = RestoreResult(*operation, *target);
  result->entries.front()->plaintext_sha256 = Digest(9u);
  EXPECT_FALSE(IsValidBackupRestoreResult(*operation, kOwnerProfileId, *target,
                                          staged, *result));
  result = RestoreResult(*operation, *target);
  result->status = mojom::BackupPlanningStatus::kRestoreConflict;
  EXPECT_FALSE(IsValidBackupRestoreResult(*operation, kOwnerProfileId, *target,
                                          staged, *result));
}

}  // namespace
}  // namespace taffy

// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/components/storage/browser/dormant_backup_restore_target_test_support.h"

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/time/time.h"
#include "crypto/hash.h"
#include "sql/database.h"
#include "sql/statement.h"
#include "taffy/components/storage/browser/backup_record_codec.h"
#include "taffy/components/storage/browser/backup_restore_stage.h"
#include "taffy/components/storage/browser/dormant_backup_restore_target.h"

namespace taffy::storage::backup {

void DormantBackupRestoreCommitTestPeer::SetAfterCommit(
    DormantBackupRestoreTarget* target,
    base::OnceClosure callback) {
  target->after_commit_for_testing_ = std::move(callback);
}

bool DormantBackupRestoreCommitTestPeer::ExecuteTargetSql(
    DormantBackupRestoreTarget* target,
    std::string_view statement) {
  // sql::Database::Execute takes a base::cstring_view, which promises a NUL
  // terminator that a string_view does not carry. The owning string supplies
  // one for the duration of the call.
  const std::string sql(statement);
  return target->database_->Execute(sql);
}

std::optional<int64_t> DormantBackupRestoreCommitTestPeer::CountTargetRows(
    DormantBackupRestoreTarget* target,
    std::string_view table) {
  sql::Statement count(target->database_->GetUniqueStatement(
      "SELECT COUNT(*) FROM " + std::string(table)));
  if (!count.Step()) {
    return std::nullopt;
  }
  const int64_t value = count.ColumnInt64(0);
  if (count.Step() || !count.Succeeded()) {
    return std::nullopt;
  }
  return value;
}

base::FilePath DormantBackupRestoreCommitTestPeer::StageDatabasePath(
    const DormantBackupRestoreTarget& target) {
  return target.restore_stage_ ? target.restore_stage_->database_path()
                               : base::FilePath();
}

namespace test {
namespace {

namespace mojom = core_service::mojom;

uint64_t LiveDeadline() {
  const int64_t now = base::TimeTicks::Now().since_origin().InMilliseconds();
  return now < 0 ? 60'000u : static_cast<uint64_t>(now) + 60'000u;
}

mojom::BackupRestorePlanResultPtr BasePlan(std::string target_profile_id) {
  auto operation = mojom::OperationEnvelope::New("restore-plan", 7u, 0u, 1u,
                                                 "restore-plan-once");
  auto target = mojom::BackupRestoreTarget::New(
      mojom::BackupRestoreTargetKind::kNewRegularProfile,
      std::move(target_profile_id));
  auto plan = mojom::BackupRestorePlanResult::New();
  plan->operation = operation.Clone();
  plan->status = mojom::BackupPlanningStatus::kSucceeded;
  plan->backup_id = "backup-test-identity";
  plan->snapshot_sha256.assign(32u, 4u);
  plan->target = target.Clone();
  plan->confirmation_sha256.assign(32u, 5u);
  plan->binding = mojom::BackupRestoreBinding::New(
      std::move(operation), kRestoreSourceProfileId, std::move(target),
      plan->backup_id, plan->snapshot_sha256, plan->confirmation_sha256);
  return plan;
}

void AppendEntry(DormantBackupRestoreScenario* scenario,
                 mojom::BackupRecordKind kind,
                 std::string stable_id,
                 uint64_t revision,
                 std::vector<uint8_t> plaintext,
                 bool tombstone) {
  auto entry = mojom::BackupRestorePlanEntry::New();
  entry->kind = kind;
  entry->stable_id = std::move(stable_id);
  entry->archive_revision = revision;
  entry->action = tombstone ? mojom::BackupRestoreAction::kStageDeletion
                            : mojom::BackupRestoreAction::kStageCreate;
  entry->schema_version = 1u;
  entry->state = tombstone ? mojom::BackupRecordState::kTombstone
                           : mojom::BackupRecordState::kActive;
  entry->plaintext_bytes = plaintext.size();
  if (tombstone) {
    entry->plaintext_sha256.assign(32u, 0u);
  } else {
    const auto digest = crypto::hash::Sha256(plaintext);
    entry->plaintext_sha256.assign(digest.begin(), digest.end());
  }
  scenario->payload.insert(scenario->payload.end(), plaintext.begin(),
                           plaintext.end());
  scenario->plan->entries.push_back(std::move(entry));
}

}  // namespace

std::optional<DormantBackupRestoreScenario> MakeMixedRestoreScenario(
    std::string target_profile_id) {
  DormantBackupRestoreScenario scenario;
  scenario.plan = BasePlan(std::move(target_profile_id));

  AppendEntry(&scenario, mojom::BackupRecordKind::kMemoryRecord,
              kRestoreMemoryTombstoneId, 5u, {}, true);
  auto configuration = mojom::AssistantConfiguration::New(
      7u, std::vector<mojom::AssistantAbility>{},
      mojom::PersonalityPreset::kQuickShopper, 1u, 2u, 0u);
  auto configuration_bytes = EncodeAssistantConfigurationV1(*configuration);
  if (!configuration_bytes.has_value()) {
    return std::nullopt;
  }
  AppendEntry(&scenario, mojom::BackupRecordKind::kAssistantConfiguration,
              kAssistantConfigurationBackupStableId, 7u,
              std::move(*configuration_bytes), false);

  std::vector<mojom::LibrarySourceRecordPtr> sources;
  sources.push_back(
      mojom::LibrarySourceRecord::New("55555555555555555555555555555555",
                                      "Specifications", "maker.example", 1000));
  auto library = mojom::LibraryEntryRecord::New(
      kRestoreLibraryActiveId, 8u, "66666666666666666666666666666666",
      "Research", "77777777777777777777777777777777", 7u,
      "88888888888888888888888888888888", "warranty", "two years",
      std::optional<std::string>("three years"),
      mojom::LibraryFactKind::kFromPage, std::move(sources), 2000, 1000, false);
  auto library_bytes = EncodeLibraryRecordV1(*library);
  if (!library_bytes.has_value()) {
    return std::nullopt;
  }
  AppendEntry(&scenario, mojom::BackupRecordKind::kLibraryEntry,
              kRestoreLibraryActiveId, 8u, std::move(*library_bytes), false);

  auto memory = mojom::MemoryRecord::New();
  memory->memory_id = kRestoreMemoryActiveId;
  memory->revision = 42u;
  memory->statement = "Keep this exact preference";
  memory->source_kind = mojom::MemorySourceKind::kUserEntered;
  memory->scope_kind = mojom::MemoryScopeKind::kAllTasks;
  memory->sensitivity = mojom::MemorySensitivity::kStandard;
  memory->created_at_epoch_ms = 1000;
  memory->updated_at_epoch_ms = 2000;
  auto memory_bytes = EncodeMemoryRecordV1(*memory);
  if (!memory_bytes.has_value()) {
    return std::nullopt;
  }
  AppendEntry(&scenario, mojom::BackupRecordKind::kMemoryRecord,
              kRestoreMemoryActiveId, 42u, std::move(*memory_bytes), false);
  AppendEntry(&scenario, mojom::BackupRecordKind::kLibraryEntry,
              kRestoreLibraryTombstoneId, 3u, {}, true);
  return scenario;
}

DormantBackupRestoreScenario MakeEmptyRestoreScenario(
    std::string target_profile_id) {
  return {.plan = BasePlan(std::move(target_profile_id)), .payload = {}};
}

mojom::BackupRestoreStageAuthorizationPtr MakeStageAuthorization(
    const mojom::BackupRestorePlanResult& plan,
    uint64_t deadline_monotonic_ms) {
  return mojom::BackupRestoreStageAuthorization::New(
      plan.binding.Clone(),
      mojom::OperationEnvelope::New(
          "restore-stage", 7u, 0u,
          deadline_monotonic_ms == 0u ? LiveDeadline() : deadline_monotonic_ms,
          "restore-stage-once"));
}

mojom::BackupRestoreCommitAuthorizationPtr MakeCommitAuthorization(
    const mojom::BackupRestorePlanResult& plan,
    uint64_t deadline_monotonic_ms) {
  return mojom::BackupRestoreCommitAuthorization::New(
      plan.binding.Clone(),
      mojom::OperationEnvelope::New(
          "restore-commit", 7u, 0u,
          deadline_monotonic_ms == 0u ? LiveDeadline() : deadline_monotonic_ms,
          "restore-commit-once"));
}

}  // namespace test
}  // namespace taffy::storage::backup

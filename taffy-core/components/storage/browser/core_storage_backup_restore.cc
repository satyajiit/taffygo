// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <algorithm>
#include <string>
#include <utility>

#include "base/files/file_util.h"
#include "base/strings/string_util.h"
#include "base/time/time.h"
#include "sql/statement.h"
#include "sql/transaction.h"
#include "taffy/components/storage/browser/core_storage_backend.h"

namespace taffy {
namespace {

namespace mojom = core_service::mojom;
using Error = storage::backup::BackupRestoreStageError;

constexpr char kStagingChild[] = "TaffyRestoreStaging";

bool BoundedText(std::string_view value, uint64_t maximum) {
  return !value.empty() && value.size() <= maximum &&
         base::IsStringUTF8(value) &&
         std::ranges::none_of(value, [](unsigned char byte) {
           return byte < 0x20 || byte == 0x7f;
         });
}

bool LiveOperation(const mojom::OperationEnvelope& operation,
                   uint64_t current_generation) {
  const auto now = base::TimeTicks::Now().since_origin().InMilliseconds();
  return BoundedText(operation.operation_id, mojom::kMaxOperationIdBytes) &&
         current_generation > 0 &&
         operation.service_generation == current_generation &&
         operation.task_revision == 0 &&
         BoundedText(operation.idempotency_key,
                     mojom::kMaxIdempotencyKeyBytes) &&
         now >= 0 &&
         operation.deadline_monotonic_ms > static_cast<uint64_t>(now);
}

}  // namespace

void CoreStorageBroker::StageBackupRestore(
    const mojom::BackupRestorePlanResult& plan,
    std::vector<uint8_t> confirmed_digest,
    base::File plaintext_payload,
    BackupRestoreStageCallback callback) {
  backend_.AsyncCall(&Backend::StageBackupRestore)
      .WithArgs(plan.Clone(), std::move(confirmed_digest),
                std::move(plaintext_payload))
      .Then(std::move(callback));
}

void CoreStorageBroker::VerifyBackupRestore(
    const mojom::OperationEnvelope& operation,
    std::vector<uint8_t> snapshot_sha256,
    std::vector<uint8_t> confirmation_sha256,
    JournalCallback callback) {
  backend_.AsyncCall(&Backend::VerifyBackupRestore)
      .WithArgs(operation.Clone(), std::move(snapshot_sha256),
                std::move(confirmation_sha256))
      .Then(std::move(callback));
}

void CoreStorageBroker::AbandonBackupRestore(std::string operation_id,
                                             JournalCallback callback) {
  backend_.AsyncCall(&Backend::AbandonBackupRestore)
      .WithArgs(std::move(operation_id))
      .Then(std::move(callback));
}

bool CoreStorageBroker::Backend::InitializeRestoreStaging() {
  if (restore_staging_initialized_ && !restore_staging_cleanup_pending_) {
    return true;
  }
  if (ephemeral_ || database_path_.empty()) {
    return false;
  }
  const auto parent = database_path_.DirName().AppendASCII(kStagingChild);
  restore_staging_cleanup_pending_ = true;
  // The only recursive target is this exact, dedicated child owned by the
  // storage broker. It contains uncommitted candidates left by process death,
  // never a live profile or rollback generation. A link is not that child.
  if (base::IsLink(parent) ||
      (base::PathExists(parent) && !base::DeletePathRecursively(parent)) ||
      !base::CreateDirectory(parent)) {
    return false;
  }
  restore_staging_initialized_ = true;
  restore_staging_cleanup_pending_ = false;
  return true;
}

bool CoreStorageBroker::Backend::IsPristineRestoreTarget(
    std::string_view profile_id) {
  sql::Transaction snapshot(&database_);
  if (!snapshot.Begin()) {
    return false;
  }
  sql::Statement identity(
      database_.GetUniqueStatement("SELECT browser_profile_id FROM "
                                   "core_profile_identity WHERE singleton=1"));
  if (!identity.Step() || identity.ColumnString(0) != profile_id) {
    return false;
  }
  // New-profile staging cannot grant permission to merge into changed data.
  // Read existence, never content, from every core-owned table. Discovering
  // tables from disk makes a future populated family a refusal automatically.
  sql::Statement tables(database_.GetUniqueStatement(
      "SELECT name FROM sqlite_schema WHERE type='table' AND name NOT IN "
      "('taffy_storage_schema','core_profile_identity','sqlite_sequence')"));
  while (tables.Step()) {
    const auto name = tables.ColumnString(0);
    if (!name.starts_with("core_") || !std::ranges::all_of(name, [](char byte) {
          return (byte >= 'a' && byte <= 'z') || (byte >= '0' && byte <= '9') ||
                 byte == '_';
        })) {
      return false;
    }
    const std::string query = "SELECT 1 FROM " + name + " LIMIT 1";
    sql::Statement row(database_.GetUniqueStatement(query));
    if (row.Step() || !row.Succeeded()) {
      return false;
    }
  }
  return tables.Succeeded() && snapshot.Commit();
}

bool CoreStorageBroker::Backend::DeletePendingRestoreStage() {
  if (!restore_stage_) {
    restore_stage_revoked_ = false;
    return !restore_staging_cleanup_pending_ || InitializeRestoreStaging();
  }
  restore_stage_revoked_ = true;
  if (!restore_stage_->Delete()) {
    // Keep exact-path custody until deletion can be verified. A failed cleanup
    // is never a valid stage and must not make the next request look
    // successful.
    return false;
  }
  restore_stage_.reset();
  restore_stage_revoked_ = false;
  return true;
}

bool CoreStorageBroker::Backend::ReapRevokedRestoreStage() {
  if (!restore_stage_) {
    return !restore_staging_cleanup_pending_ || InitializeRestoreStaging();
  }
  if (restore_stage_ && (restore_stage_revoked_ ||
                         !LiveOperation(*restore_stage_->plan().operation,
                                        backup_restore_generation_))) {
    return DeletePendingRestoreStage();
  }
  return true;
}

void CoreStorageBroker::Backend::ObserveBackupRestoreGeneration(
    uint64_t generation) {
  backup_restore_generation_ = generation;
  ReapRevokedRestoreStage();
  if (!ephemeral_ && !restore_stage_ && !restore_staging_initialized_ &&
      base::PathExists(database_path_.DirName().AppendASCII(kStagingChild))) {
    InitializeRestoreStaging();
  }
}

base::expected<void, Error> CoreStorageBroker::Backend::StageBackupRestore(
    mojom::BackupRestorePlanResultPtr plan,
    std::vector<uint8_t> confirmed_digest,
    base::File plaintext_payload) {
  if (ephemeral_ || !plan || !plan->operation || !plan->target ||
      !LiveOperation(*plan->operation, backup_restore_generation_)) {
    return base::unexpected(Error::kPlanRefused);
  }
  if (!ReapRevokedRestoreStage()) {
    return base::unexpected(Error::kStorageUnavailable);
  }
  if (restore_stage_) {
    return base::unexpected(Error::kStageBusy);
  }
  if (!EnsureOpen()) {
    return base::unexpected(Error::kStorageUnavailable);
  }
  if (!IsPristineRestoreTarget(plan->target->profile_id)) {
    return base::unexpected(Error::kPlanRefused);
  }
  if (!InitializeRestoreStaging()) {
    return base::unexpected(Error::kStorageUnavailable);
  }
  auto stage = storage::backup::BackupRestoreStage::Create(
      database_path_.DirName().AppendASCII(kStagingChild), *plan,
      confirmed_digest, std::move(plaintext_payload));
  if (!stage.has_value()) {
    // Even a failed Create may have created a child whose destructor could not
    // remove it. The dedicated parent's exact ownership survives that error.
    restore_staging_cleanup_pending_ = true;
    if (!InitializeRestoreStaging()) {
      return base::unexpected(Error::kStorageUnavailable);
    }
    return base::unexpected(stage.error());
  }
  // Staging may take time. Expiry withdraws the candidate, never publishes a
  // ready stage whose operation lost authority while the file was read.
  restore_stage_ = std::move(*stage);
  if (!LiveOperation(*plan->operation, backup_restore_generation_)) {
    DeletePendingRestoreStage();
    return base::unexpected(Error::kPlanRefused);
  }
  return base::ok();
}

bool CoreStorageBroker::Backend::VerifyBackupRestore(
    mojom::OperationEnvelopePtr operation,
    std::vector<uint8_t> snapshot_sha256,
    std::vector<uint8_t> confirmation_sha256) {
  if (ephemeral_ || !ReapRevokedRestoreStage() || !restore_stage_ ||
      !operation || !LiveOperation(*operation, backup_restore_generation_)) {
    return false;
  }
  const auto& plan = restore_stage_->plan();
  if (!IsPristineRestoreTarget(plan.target->profile_id)) {
    DeletePendingRestoreStage();
    return false;
  }
  return plan.operation->Equals(*operation) &&
         plan.snapshot_sha256 == snapshot_sha256 &&
         plan.confirmation_sha256 == confirmation_sha256 &&
         restore_stage_->Verify() &&
         LiveOperation(*operation, backup_restore_generation_);
}

bool CoreStorageBroker::Backend::AbandonBackupRestore(
    std::string operation_id) {
  if (!BoundedText(operation_id, mojom::kMaxOperationIdBytes)) {
    return false;
  }
  if (!restore_stage_) {
    return DeletePendingRestoreStage();
  }
  if (restore_stage_->plan().operation->operation_id != operation_id) {
    return false;
  }
  return DeletePendingRestoreStage();
}

}  // namespace taffy

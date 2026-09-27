// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/task/task_traits.h"
#include "base/task/thread_pool.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/common/chrome_constants.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/browser_thread.h"
#include "taffy/browser/android/browser_profiles_restore_lifecycle_internal.h"
#include "taffy/browser/backup_restore_recovery_journal.h"
#include "taffy/browser/core_backup_protocol.h"
#include "taffy/browser/core_service_manager.h"

namespace taffy {
namespace restore_reconciliation_internal {

BackupRestoreCommitReconciliationError ProjectStorageReconcileError(
    storage::backup::DormantBackupRestoreReconcileError error) {
  using StorageError = storage::backup::DormantBackupRestoreReconcileError;
  using Error = BackupRestoreCommitReconciliationError;
  switch (error) {
    case StorageError::kTargetBusy:
      return Error::kBusy;
    case StorageError::kInvalidTarget:
    case StorageError::kTargetChanged:
      return Error::kProfileStateRefused;
    case StorageError::kSchemaMismatch:
      return Error::kSchemaMismatch;
    case StorageError::kInvalidWitness:
      return Error::kHistoryRefused;
    case StorageError::kStorageUnavailable:
      return Error::kStorageUnavailable;
  }
  return Error::kStorageUnavailable;
}

}  // namespace restore_reconciliation_internal

namespace {

// Usable only inside this unnamed namespace. At `taffy` scope the short name
// is ambiguous with the BIP contract's `taffy::mojom`, so the definitions
// below spell `core_service::mojom::` in full.
namespace mojom = core_service::mojom;
using Error = BackupRestoreCommitReconciliationError;
using restore_reconciliation_internal::ExactHistory;
using restore_reconciliation_internal::LiveSourceManager;
using restore_reconciliation_internal::NewInspectionOperation;
using restore_reconciliation_internal::ProjectStorageReconcileError;
using restore_reconciliation_internal::ResolveTarget;
using restore_reconciliation_internal::ValidInspectionResult;

bool JournalEndsWithExactFact(
    const PrefService* local_state,
    const std::string& reservation_id,
    const mojom::BackupRestoreRecoveryRecord& expected) {
  auto records = ReadBackupRestoreRecoveryJournal(local_state, reservation_id);
  if (!records || records->size() != expected.sequence || records->empty()) {
    return false;
  }
  BackupRestoreRecoveryRecords expected_record;
  expected_record.push_back(expected.Clone());
  BackupRestoreRecoveryRecords actual_record;
  actual_record.push_back(records->back().Clone());
  return ExactHistory(expected_record, actual_record);
}

void VerifyPreferenceFile(
    const base::FilePath& path,
    std::vector<BackupRestorePreferenceWriteWitness> witnesses,
    base::OnceCallback<void(bool)> callback) {
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE,
      {base::MayBlock(), base::TaskPriority::USER_BLOCKING,
       base::TaskShutdownBehavior::SKIP_ON_SHUTDOWN},
      base::BindOnce(&BackupRestorePreferenceFileMatchesAndSync, path,
                     std::move(witnesses)),
      std::move(callback));
}

}  // namespace

void BrowserProfilesRestoreLifecycle::OnCommitRecoveryStorageObserved(
    CommitReconciliationStorageResult result) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!pending_commit_reconciliation_ ||
      !pending_commit_reconciliation_->commit_intent) {
    return;
  }
  if (!result) {
    // Lease, schema, custody and SQL failures are not physical observations.
    // Preserve their typed refusal and leave the exact journal unchanged.
    FinishCommitReconciliation(
        base::unexpected(ProjectStorageReconcileError(result.error())));
    return;
  }
  auto resolved = ResolveTarget(profile_manager_, local_state_,
                                pending_commit_reconciliation_->reservation_id);
  auto records = ReadBackupRestoreRecoveryJournal(
      local_state_, pending_commit_reconciliation_->reservation_id);
  if (!resolved || !records ||
      resolved->source_profile_path !=
          pending_commit_reconciliation_->source_profile_path ||
      resolved->target_profile_path !=
          pending_commit_reconciliation_->target_profile_path ||
      resolved->target_profile_id !=
          pending_commit_reconciliation_->target_profile_id ||
      !ExactHistory(*records, pending_commit_reconciliation_->records)) {
    FinishCommitReconciliation(base::unexpected(Error::kPersistenceFailed));
    return;
  }
  pending_commit_reconciliation_->physical_outcome = *result;
  auto record = RecordBackupRestoreCommitOutcome(
      local_state_, pending_commit_reconciliation_->reservation_id,
      *pending_commit_reconciliation_->commit_intent, *result);
  if (!record.has_value()) {
    FinishCommitReconciliation(base::unexpected(Error::kRegistryRefused));
    return;
  }
  pending_commit_reconciliation_->outcome_record = std::move(*record);
  pending_commit_reconciliation_->persistence_witnesses =
      CaptureDormantBackupRestoreTargetPreferenceWitnesses(
          *local_state_, pending_commit_reconciliation_->target_profile_path);
  if (pending_commit_reconciliation_->persistence_witnesses.empty()) {
    FinishCommitReconciliation(base::unexpected(Error::kPersistenceFailed));
    return;
  }
  local_state_->CommitPendingWrite(base::BindOnce(
      &BrowserProfilesRestoreLifecycle::OnCommitRecoveryOutcomeWriteDrained,
      weak_factory_.GetWeakPtr()));
}

void BrowserProfilesRestoreLifecycle::OnCommitRecoveryOutcomeWriteDrained() {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!pending_commit_reconciliation_) {
    return;
  }
  VerifyPreferenceFile(
      profile_manager_->user_data_dir().Append(chrome::kLocalStateFilename),
      std::move(pending_commit_reconciliation_->persistence_witnesses),
      base::BindOnce(
          &BrowserProfilesRestoreLifecycle::OnCommitRecoveryOutcomeReadBack,
          weak_factory_.GetWeakPtr()));
}

void BrowserProfilesRestoreLifecycle::OnCommitRecoveryOutcomeReadBack(
    bool matches) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!pending_commit_reconciliation_) {
    return;
  }
  auto resolved = ResolveTarget(profile_manager_, local_state_,
                                pending_commit_reconciliation_->reservation_id);
  if (!matches || !resolved ||
      resolved->source_profile_path !=
          pending_commit_reconciliation_->source_profile_path ||
      resolved->target_profile_path !=
          pending_commit_reconciliation_->target_profile_path ||
      resolved->target_profile_id !=
          pending_commit_reconciliation_->target_profile_id ||
      !pending_commit_reconciliation_->outcome_record ||
      !JournalEndsWithExactFact(
          local_state_, pending_commit_reconciliation_->reservation_id,
          *pending_commit_reconciliation_->outcome_record)) {
    FinishCommitReconciliation(base::unexpected(Error::kPersistenceFailed));
    return;
  }
  auto records = ReadBackupRestoreRecoveryJournal(
      local_state_, pending_commit_reconciliation_->reservation_id);
  CoreServiceManager* const manager =
      records ? LiveSourceManager(profile_manager_, *resolved, *records)
              : nullptr;
  if (!records || !manager) {
    FinishCommitReconciliation(base::unexpected(Error::kCoreUnavailable));
    return;
  }
  pending_commit_reconciliation_->records = std::move(*records);
  pending_commit_reconciliation_->inspection_operation =
      NewInspectionOperation(manager->service_generation());
  if (!pending_commit_reconciliation_->inspection_operation) {
    FinishCommitReconciliation(base::unexpected(Error::kCoreUnavailable));
    return;
  }
  BackupRestoreRecoveryRecords request_records;
  for (const auto& record : pending_commit_reconciliation_->records) {
    request_records.push_back(record.Clone());
  }
  manager->backup_protocol().InspectBackupRestoreRecovery(
      core_service::mojom::BackupRestoreRecoveryInspectionRequest::New(
          pending_commit_reconciliation_->inspection_operation.Clone(),
          std::move(request_records)),
      base::BindOnce(
          &BrowserProfilesRestoreLifecycle::OnCommitRecoveryUpdatedInspected,
          weak_factory_.GetWeakPtr()));
}

void BrowserProfilesRestoreLifecycle::OnCommitRecoveryUpdatedInspected(
    core_service::mojom::BackupRestoreRecoveryInspectionResultPtr result) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!pending_commit_reconciliation_) {
    return;
  }
  if (!ValidInspectionResult(
          pending_commit_reconciliation_->inspection_operation.get(),
          result.get())) {
    FinishCommitReconciliation(base::unexpected(Error::kHistoryRefused));
    return;
  }
  auto resolved = ResolveTarget(profile_manager_, local_state_,
                                pending_commit_reconciliation_->reservation_id);
  auto records = ReadBackupRestoreRecoveryJournal(
      local_state_, pending_commit_reconciliation_->reservation_id);
  if (!resolved || !records ||
      resolved->source_profile_path !=
          pending_commit_reconciliation_->source_profile_path ||
      resolved->target_profile_path !=
          pending_commit_reconciliation_->target_profile_path ||
      resolved->target_profile_id !=
          pending_commit_reconciliation_->target_profile_id ||
      !ExactHistory(*records, pending_commit_reconciliation_->records) ||
      !LiveSourceManager(profile_manager_, *resolved, *records)) {
    FinishCommitReconciliation(base::unexpected(Error::kPersistenceFailed));
    return;
  }
  ProfileBackupRestoreCommitObservation physical;
  physical.outcome = pending_commit_reconciliation_->physical_outcome;
  physical.journal_durable = true;
  FinishCommitReconciliation(BackupRestoreCommitReconciliation{
      .classification = std::move(result->classification),
      .physical_observation = physical,
  });
}

void BrowserProfilesRestoreLifecycle::FinishCommitReconciliation(
    CommitReconciliationResult result) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!pending_commit_reconciliation_) {
    return;
  }
  CommitReconciliationCallback callback =
      std::move(pending_commit_reconciliation_->callback);
  pending_commit_reconciliation_.reset();
  std::move(callback).Run(std::move(result));
}

}  // namespace taffy

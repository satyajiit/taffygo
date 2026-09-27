// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/task/task_traits.h"
#include "base/task/thread_pool.h"
#include "base/uuid.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_attributes_entry.h"
#include "chrome/browser/profiles/profile_attributes_storage.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/browser_thread.h"
#include "taffy/browser/android/browser_profiles_restore_lifecycle_internal.h"
#include "taffy/browser/backup_restore_profile_registry.h"
#include "taffy/browser/backup_restore_recovery_journal.h"
#include "taffy/browser/core_backup_planning_validation.h"
#include "taffy/browser/core_backup_protocol.h"
#include "taffy/browser/core_backup_recovery_validation.h"
#include "taffy/browser/core_service_manager.h"
#include "taffy/browser/core_service_manager_factory.h"

namespace taffy {
namespace restore_reconciliation_internal {

namespace mojom = core_service::mojom;
using Error = BackupRestoreCommitReconciliationError;
using PhysicalState = BackupRestoreProfilePhysicalState;
using Quarantine = BackupRestoreProfileQuarantineStatus;

base::expected<ResolvedDormantBackupRestoreTarget, Error> ResolveTarget(
    ProfileManager* profile_manager,
    PrefService* local_state,
    const std::string& reservation_id) {
  if (!profile_manager || !local_state || reservation_id.empty() ||
      profile_manager->user_data_dir().empty() ||
      !profile_manager->user_data_dir().IsAbsolute()) {
    return base::unexpected(Error::kInvalidArgument);
  }
  auto reservations = ReadBackupRestoreProfileReservations(local_state);
  if (!reservations || reservations->size() != 1u ||
      reservations->front().reservation_id != reservation_id ||
      reservations->front().physical_state != PhysicalState::kProfileCreated ||
      !reservations->front().target_profile_id) {
    return base::unexpected(Error::kRegistryRefused);
  }
  const auto& reservation = reservations->front();
  const base::FilePath source_path = profile_manager->user_data_dir().Append(
      reservation.source_profile_base_name);
  const base::FilePath target_path = profile_manager->user_data_dir().Append(
      reservation.target_profile_base_name);
  if (source_path == target_path ||
      source_path.DirName() != profile_manager->user_data_dir() ||
      target_path.DirName() != profile_manager->user_data_dir() ||
      !profile_manager->IsAllowedProfilePath(source_path) ||
      !profile_manager->IsAllowedProfilePath(target_path)) {
    return base::unexpected(Error::kProfileStateRefused);
  }
  Profile* const source = profile_manager->GetProfileByPath(source_path);
  Profile* const target = profile_manager->GetProfileByPath(target_path);
  ProfileAttributesEntry* const source_entry =
      profile_manager->GetProfileAttributesStorage()
          .GetProfileAttributesWithPath(source_path);
  ProfileAttributesEntry* const target_entry =
      profile_manager->GetProfileAttributesStorage()
          .GetProfileAttributesWithPath(target_path);
  if (!source || source->IsOffTheRecord() ||
      !profile_manager->IsValidProfile(source) || !source_entry ||
      source_entry->IsOmitted() || source_entry->IsEphemeral() ||
      !target_entry || !target_entry->IsEphemeral() ||
      profile_manager->GetLastUsedProfileDir() != source_path ||
      BackupRestoreQuarantineForProfilePath(local_state, source_path) !=
          Quarantine::kNotQuarantined ||
      BackupRestoreQuarantineForProfilePath(local_state, target_path) !=
          Quarantine::kQuarantined) {
    return base::unexpected(Error::kProfileStateRefused);
  }
  if (target &&
      (target == source || target->IsOffTheRecord() ||
       !profile_manager->IsValidProfile(target) ||
       CoreServiceManagerFactory::HasExistingInstanceForProfile(target))) {
    return base::unexpected(Error::kProfileStateRefused);
  }
  return ResolvedDormantBackupRestoreTarget{
      .reservation_id = reservation_id,
      .source_profile_path = source_path,
      .target_profile_path = target_path,
      .target_profile_id = *reservation.target_profile_id,
  };
}

CoreServiceManager* LiveSourceManager(
    ProfileManager* profile_manager,
    const ResolvedDormantBackupRestoreTarget& target,
    const BackupRestoreRecoveryRecords& records) {
  if (records.empty() || !records.front() || !records.front()->binding) {
    return nullptr;
  }
  Profile* const source =
      profile_manager->GetProfileByPath(target.source_profile_path);
  CoreServiceManager* const manager =
      CoreServiceManagerFactory::GetForProfileIfExists(source);
  return manager &&
                 manager->availability() ==
                     CoreServiceManager::Availability::kReady &&
                 records.front()->binding->owner_profile_id ==
                     manager->browser_profile_id() &&
                 records.front()->binding->target_kind ==
                     mojom::BackupRestoreTargetKind::kNewRegularProfile &&
                 records.front()->binding->target_profile_id ==
                     target.target_profile_id
             ? manager
             : nullptr;
}

bool ExactHistory(const BackupRestoreRecoveryRecords& left,
                  const BackupRestoreRecoveryRecords& right) {
  if (left.size() != right.size()) {
    return false;
  }
  for (size_t index = 0u; index < left.size(); ++index) {
    if (!left[index] || !right[index]) {
      return false;
    }
    auto encoded_left = EncodeBackupRestoreRecoveryRecord(*left[index]);
    auto encoded_right = EncodeBackupRestoreRecoveryRecord(*right[index]);
    if (!encoded_left || !encoded_right || *encoded_left != *encoded_right) {
      return false;
    }
  }
  return true;
}

mojom::OperationEnvelopePtr NewInspectionOperation(uint64_t generation) {
  constexpr uint64_t kDeadlineMillis = 30'000u;
  const uint64_t now = BackupPlanningNowMonotonicMillis();
  const base::Uuid operation_id = base::Uuid::GenerateRandomV4();
  const base::Uuid idempotency_key = base::Uuid::GenerateRandomV4();
  if (generation == 0u || !operation_id.is_valid() ||
      !idempotency_key.is_valid() ||
      now > std::numeric_limits<uint64_t>::max() - kDeadlineMillis) {
    return nullptr;
  }
  auto operation = mojom::OperationEnvelope::New(
      "backup-restore-recovery-" + operation_id.AsLowercaseString(), generation,
      0u, now + kDeadlineMillis,
      "backup-restore-recovery-" + idempotency_key.AsLowercaseString());
  return IsLiveBackupOperation(operation.get(), generation, now)
             ? std::move(operation)
             : nullptr;
}

bool ValidInspectionResult(
    const mojom::OperationEnvelope* expected,
    const mojom::BackupRestoreRecoveryInspectionResult* result) {
  return expected && result &&
         IsValidBackupRestoreRecoveryInspectionResult(*expected, *result) &&
         result->status ==
             mojom::BackupRestoreRecoveryInspectionStatus::kSucceeded;
}

}  // namespace restore_reconciliation_internal

namespace {

// No `mojom` alias here on purpose: at `taffy` scope the short name is
// ambiguous with the BIP contract's `taffy::mojom`, so the definitions below
// spell `core_service::mojom::` in full.
using Error = BackupRestoreCommitReconciliationError;
using restore_reconciliation_internal::ExactHistory;
using restore_reconciliation_internal::LiveSourceManager;
using restore_reconciliation_internal::NewInspectionOperation;
using restore_reconciliation_internal::ResolveTarget;
using restore_reconciliation_internal::ValidInspectionResult;

}  // namespace

BrowserProfilesRestoreLifecycle::PendingCommitReconciliation::
    PendingCommitReconciliation()
    : blocking_owner(base::ThreadPool::CreateSequencedTaskRunner(
          {base::MayBlock(), base::TaskPriority::USER_VISIBLE,
           base::TaskShutdownBehavior::BLOCK_SHUTDOWN})) {}

BrowserProfilesRestoreLifecycle::PendingCommitReconciliation::
    ~PendingCommitReconciliation() = default;

void BrowserProfilesRestoreLifecycle::ReconcileInterruptedCommit(
    std::string reservation_id,
    CommitReconciliationCallback callback) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!callback) {
    return;
  }
  if (operation_in_flight()) {
    std::move(callback).Run(base::unexpected(Error::kBusy));
    return;
  }
  auto resolved = ResolveTarget(profile_manager_, local_state_, reservation_id);
  auto records = ReadBackupRestoreRecoveryJournal(local_state_, reservation_id);
  if (!resolved || !records || records->empty()) {
    std::move(callback).Run(base::unexpected(
        !resolved ? resolved.error() : Error::kRegistryRefused));
    return;
  }
  if (!LiveSourceManager(profile_manager_, *resolved, *records)) {
    std::move(callback).Run(base::unexpected(Error::kCoreUnavailable));
    return;
  }
  pending_commit_reconciliation_ =
      std::make_unique<PendingCommitReconciliation>();
  pending_commit_reconciliation_->reservation_id = std::move(reservation_id);
  pending_commit_reconciliation_->source_profile_path =
      resolved->source_profile_path;
  pending_commit_reconciliation_->target_profile_path =
      resolved->target_profile_path;
  pending_commit_reconciliation_->target_profile_id =
      resolved->target_profile_id;
  pending_commit_reconciliation_->records = std::move(*records);
  pending_commit_reconciliation_->callback = std::move(callback);
  VerifyDormantBackupRestoreTargetPreferences(
      profile_manager_, local_state_, resolved->target_profile_path,
      base::BindOnce(&BrowserProfilesRestoreLifecycle::OnCommitRecoveryReadBack,
                     weak_factory_.GetWeakPtr()));
}

void BrowserProfilesRestoreLifecycle::OnCommitRecoveryReadBack(bool matches) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!pending_commit_reconciliation_) {
    return;
  }
  auto resolved = ResolveTarget(profile_manager_, local_state_,
                                pending_commit_reconciliation_->reservation_id);
  auto records = ReadBackupRestoreRecoveryJournal(
      local_state_, pending_commit_reconciliation_->reservation_id);
  if (!matches || !resolved ||
      resolved->source_profile_path !=
          pending_commit_reconciliation_->source_profile_path ||
      resolved->target_profile_path !=
          pending_commit_reconciliation_->target_profile_path ||
      resolved->target_profile_id !=
          pending_commit_reconciliation_->target_profile_id ||
      !records ||
      !ExactHistory(*records, pending_commit_reconciliation_->records)) {
    FinishCommitReconciliation(base::unexpected(Error::kPersistenceFailed));
    return;
  }
  CoreServiceManager* const manager =
      LiveSourceManager(profile_manager_, *resolved, *records);
  if (!manager) {
    FinishCommitReconciliation(base::unexpected(Error::kCoreUnavailable));
    return;
  }
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
          &BrowserProfilesRestoreLifecycle::OnCommitRecoveryInspected,
          weak_factory_.GetWeakPtr()));
}

void BrowserProfilesRestoreLifecycle::OnCommitRecoveryInspected(
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
  if (result->classification->kind !=
      core_service::mojom::BackupRestoreRecoveryClassificationKind::
          kReconcileRequired) {
    auto resolved =
        ResolveTarget(profile_manager_, local_state_,
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
    FinishCommitReconciliation(BackupRestoreCommitReconciliation{
        .classification = std::move(result->classification)});
    return;
  }
  const auto* reconciliation = result->classification->reconciliation.get();
  const auto& intent = pending_commit_reconciliation_->records.front();
  if (!reconciliation ||
      reconciliation->intent !=
          core_service::mojom::BackupRestorePhysicalIntent::kCommitCandidate ||
      !intent || !intent->intent ||
      intent->intent->intent_id != reconciliation->intent_id ||
      !intent->binding) {
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
  pending_commit_reconciliation_->commit_intent = intent.Clone();
  auto witness = core_service::mojom::BackupRestoreCandidateWitness::New();
  witness->selection = intent->binding->selection;
  witness->record_count = intent->binding->record_count;
  witness->candidate_records_sha256 = intent->binding->candidate_records_sha256;
  pending_commit_reconciliation_->witness = witness.Clone();
  auto target = core_service::mojom::BackupRestoreTarget::New(
      intent->binding->target_kind, intent->binding->target_profile_id);
  pending_commit_reconciliation_->blocking_owner
      .AsyncCall(&CommitReconciliationBlockingOwner::Reconcile)
      .WithArgs(pending_commit_reconciliation_->target_profile_path,
                std::move(target), std::move(witness))
      .Then(base::BindOnce(
          &BrowserProfilesRestoreLifecycle::OnCommitRecoveryStorageObserved,
          weak_factory_.GetWeakPtr()));
}

}  // namespace taffy

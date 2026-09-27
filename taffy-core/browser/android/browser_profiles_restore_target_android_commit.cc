// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/task/task_traits.h"
#include "base/task/thread_pool.h"
#include "base/uuid.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/common/chrome_constants.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/browser_thread.h"
#include "taffy/browser/android/browser_profiles_restore_lifecycle_internal.h"
#include "taffy/browser/android/browser_profiles_restore_target_android.h"
#include "taffy/browser/android/browser_profiles_restore_target_android_internal.h"
#include "taffy/browser/backup_restore_recovery_codec.h"
#include "taffy/browser/backup_restore_recovery_journal.h"
#include "taffy/browser/core_backup_planning_validation.h"
#include "taffy/browser/core_backup_protocol_validation.h"
#include "taffy/browser/core_service_manager.h"
#include "taffy/browser/core_service_manager_factory.h"

namespace taffy {
namespace {

// Usable only inside this unnamed namespace. At `taffy` scope the short name
// is ambiguous with the BIP contract's `taffy::mojom`, so the member
// definitions below spell `core_service::mojom::` in full.
namespace mojom = core_service::mojom;
using RegistryError = BackupRestoreProfileRegistryError;
using TargetError = ProfileBackupRestoreTargetError;

bool IsExactPlanEntry(const mojom::BackupRestorePlanEntryPtr& left,
                      const mojom::BackupRestorePlanEntryPtr& right) {
  return left && right && left->kind == right->kind &&
         left->stable_id == right->stable_id &&
         left->archive_revision == right->archive_revision &&
         left->action == right->action &&
         left->schema_version == right->schema_version &&
         left->state == right->state &&
         left->plaintext_bytes == right->plaintext_bytes &&
         left->plaintext_sha256 == right->plaintext_sha256;
}

bool IsExactPlan(const mojom::BackupRestorePlanResult* left,
                 const mojom::BackupRestorePlanResult* right) {
  if (!left || !right ||
      !IsExactBackupOperation(left->operation.get(), right->operation.get()) ||
      left->status != right->status || left->backup_id != right->backup_id ||
      left->snapshot_sha256 != right->snapshot_sha256 || !left->target ||
      !right->target || left->target->kind != right->target->kind ||
      left->target->profile_id != right->target->profile_id ||
      left->entries.size() != right->entries.size() ||
      left->has_conflicts != right->has_conflicts ||
      left->confirmation_sha256 != right->confirmation_sha256 ||
      !IsExactBackupRestoreBinding(left->binding.get(), right->binding.get())) {
    return false;
  }
  for (size_t index = 0u; index < left->entries.size(); ++index) {
    if (!IsExactPlanEntry(left->entries[index], right->entries[index])) {
      return false;
    }
  }
  return true;
}

TargetError MapRegistryError(RegistryError error) {
  switch (error) {
    case RegistryError::kBusy:
      return TargetError::kBusy;
    case RegistryError::kInvalidArgument:
    case RegistryError::kWrongPhysicalState:
      return TargetError::kInvalidAuthorization;
    case RegistryError::kUnavailable:
    case RegistryError::kCorrupt:
    case RegistryError::kNotFound:
      return TargetError::kStorageUnavailable;
  }
  return TargetError::kStorageUnavailable;
}

bool JournalEndsWithExactFact(
    const PrefService* local_state,
    const std::string& reservation_id,
    const mojom::BackupRestoreRecoveryRecord& expected) {
  auto history = ReadBackupRestoreRecoveryJournal(local_state, reservation_id);
  auto encoded_expected = EncodeBackupRestoreRecoveryRecord(expected);
  if (!history || history->size() != expected.sequence || history->empty() ||
      !encoded_expected) {
    return false;
  }
  auto encoded_actual = EncodeBackupRestoreRecoveryRecord(*history->back());
  return encoded_actual && *encoded_actual == *encoded_expected;
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

void BrowserProfilesRestoreLifecycle::DormantTargetState::Commit(
    core_service::mojom::BackupRestorePlanResultPtr plan,
    core_service::mojom::BackupRestoreCommitAuthorizationPtr authorization,
    CommitCallback callback) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!callback) {
    return;
  }
  if (pending_stage_ || pending_commit_ || cleanup_callback_ ||
      cleanup_in_flight_ || close_for_recovery_callback_ ||
      closed_for_recovery_) {
    std::move(callback).Run(base::unexpected(TargetError::kBusy));
    return;
  }
  if (abandon_requested_ || !staged_plan_) {
    std::move(callback).Run(base::unexpected(TargetError::kUnavailable));
    return;
  }
  if (commit_authorization_consumed_) {
    std::move(callback).Run(base::unexpected(TargetError::kBusy));
    return;
  }
  if (!IsExactPlan(staged_plan_.get(), plan.get()) || !authorization ||
      !plan->binding ||
      !IsExactBackupRestoreBinding(plan->binding.get(),
                                   authorization->binding.get()) ||
      !HasLiveCommitAuthority(*authorization)) {
    std::move(callback).Run(
        base::unexpected(TargetError::kInvalidAuthorization));
    return;
  }

  // A structurally valid decision is single-use from this point, including
  // if registry persistence later fails. Repeating it can never grant another
  // physical attempt.
  commit_authorization_consumed_ = true;
  pending_commit_ = std::make_unique<PendingCommit>();
  pending_commit_->plan = std::move(plan);
  pending_commit_->authorization = std::move(authorization);
  pending_commit_->callback = std::move(callback);
  VerifyCustody(base::BindOnce(&DormantTargetState::OnCommitPreValidation,
                               weak_factory_.GetWeakPtr()));
}

bool BrowserProfilesRestoreLifecycle::DormantTargetState::
    HasLiveCommitAuthority(
        const core_service::mojom::BackupRestoreCommitAuthorization&
            authorization) const {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  Profile* const source =
      profile_manager_->GetProfileByPath(resolved_target_.source_profile_path);
  CoreServiceManager* const manager =
      CoreServiceManagerFactory::GetForProfileIfExists(source);
  return source && manager &&
         manager->availability() == CoreServiceManager::Availability::kReady &&
         authorization.binding &&
         authorization.binding->owner_profile_id ==
             manager->browser_profile_id() &&
         IsLiveBackupRestoreCommitAuthorization(
             &authorization, manager->service_generation(),
             BackupPlanningNowMonotonicMillis());
}

void BrowserProfilesRestoreLifecycle::DormantTargetState::OnCommitPreValidation(
    bool matches) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!pending_commit_) {
    return;
  }
  if (!matches || !HasExactLiveCustody() ||
      !HasLiveCommitAuthority(*pending_commit_->authorization)) {
    FinishCommit(base::unexpected(TargetError::kUnavailable));
    return;
  }
  blocking_owner_.AsyncCall(&BlockingOwner::PrepareCommitWitness)
      .Then(base::BindOnce(&DormantTargetState::OnCommitWitnessPrepared,
                           weak_factory_.GetWeakPtr()));
}

void BrowserProfilesRestoreLifecycle::DormantTargetState::
    OnCommitWitnessPrepared(CommitPreparationResult result) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!pending_commit_) {
    return;
  }
  if (!result.has_value()) {
    FinishCommit(base::unexpected(result.error()));
    return;
  }
  pending_commit_->witness = std::move(*result);
  if (!pending_commit_->witness) {
    FinishCommit(base::unexpected(TargetError::kStorageUnavailable));
    return;
  }
  auto intent = BeginBackupRestoreCommitIntent(
      local_state_, resolved_target_.reservation_id,
      *pending_commit_->plan->binding, *pending_commit_->witness,
      base::Uuid::GenerateRandomV4().AsLowercaseString());
  if (!intent.has_value()) {
    FinishCommit(base::unexpected(MapRegistryError(intent.error())));
    return;
  }
  pending_commit_->intent = std::move(*intent);
  pending_commit_->persistence_witnesses =
      CaptureDormantBackupRestoreTargetPreferenceWitnesses(
          *local_state_, resolved_target_.target_profile_path);
  if (pending_commit_->persistence_witnesses.empty()) {
    FinishCommit(base::unexpected(TargetError::kStorageUnavailable));
    return;
  }
  local_state_->CommitPendingWrite(
      base::BindOnce(&DormantTargetState::OnCommitIntentWriteDrained,
                     weak_factory_.GetWeakPtr()));
}

void BrowserProfilesRestoreLifecycle::DormantTargetState::
    OnCommitIntentWriteDrained() {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!pending_commit_) {
    return;
  }
  VerifyPreferenceFile(
      profile_manager_->user_data_dir().Append(chrome::kLocalStateFilename),
      std::move(pending_commit_->persistence_witnesses),
      base::BindOnce(&DormantTargetState::OnCommitIntentReadBack,
                     weak_factory_.GetWeakPtr()));
}

void BrowserProfilesRestoreLifecycle::DormantTargetState::
    OnCommitIntentReadBack(bool matches) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!pending_commit_) {
    return;
  }
  if (!matches || !pending_commit_->intent ||
      !JournalEndsWithExactFact(local_state_, resolved_target_.reservation_id,
                                *pending_commit_->intent)) {
    FinishCommit(base::unexpected(TargetError::kStorageUnavailable));
    return;
  }
  DispatchCommitOrRecordRefusal();
}

void BrowserProfilesRestoreLifecycle::DormantTargetState::
    DispatchCommitOrRecordRefusal() {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!pending_commit_) {
    return;
  }
  // This is the physical dispatch linearization point. No UI task is posted
  // between this last authority/custody check and the one blocking-sequence
  // call. Once posted, the SQL owner must drain its atomic terminal.
  if (!HasExactLiveCustody() ||
      !HasLiveCommitAuthority(*pending_commit_->authorization)) {
    PersistCommitOutcome(core_service::mojom::BackupRestoreCommitOutcome::
                             kDefinitelyNotCommitted);
    return;
  }
  pending_commit_->dispatched = true;
  commit_storage_dispatched_ = true;
  blocking_owner_.AsyncCall(&BlockingOwner::Commit)
      .WithArgs(std::move(pending_commit_->plan),
                std::move(pending_commit_->authorization),
                std::move(pending_commit_->witness))
      .Then(base::BindOnce(&DormantTargetState::OnCommitExecuted,
                           weak_factory_.GetWeakPtr()));
}

void BrowserProfilesRestoreLifecycle::DormantTargetState::OnCommitExecuted(
    core_service::mojom::BackupRestoreCommitOutcome outcome) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!pending_commit_ || !pending_commit_->dispatched) {
    return;
  }
  PersistCommitOutcome(outcome);
}

void BrowserProfilesRestoreLifecycle::DormantTargetState::PersistCommitOutcome(
    core_service::mojom::BackupRestoreCommitOutcome outcome) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!pending_commit_ || !pending_commit_->intent) {
    return;
  }
  pending_commit_->observed_outcome = outcome;
  auto record = RecordBackupRestoreCommitOutcome(
      local_state_, resolved_target_.reservation_id, *pending_commit_->intent,
      outcome);
  if (!record.has_value()) {
    if (pending_commit_->dispatched) {
      FinishCommit(ProfileBackupRestoreCommitObservation{});
    } else {
      FinishCommit(base::unexpected(MapRegistryError(record.error())));
    }
    return;
  }
  pending_commit_->outcome_record = std::move(*record);
  pending_commit_->persistence_witnesses =
      CaptureDormantBackupRestoreTargetPreferenceWitnesses(
          *local_state_, resolved_target_.target_profile_path);
  if (pending_commit_->persistence_witnesses.empty()) {
    if (pending_commit_->dispatched) {
      FinishCommit(ProfileBackupRestoreCommitObservation{});
    } else {
      FinishCommit(base::unexpected(TargetError::kStorageUnavailable));
    }
    return;
  }
  local_state_->CommitPendingWrite(
      base::BindOnce(&DormantTargetState::OnCommitOutcomeWriteDrained,
                     weak_factory_.GetWeakPtr()));
}

void BrowserProfilesRestoreLifecycle::DormantTargetState::
    OnCommitOutcomeWriteDrained() {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!pending_commit_) {
    return;
  }
  VerifyPreferenceFile(
      profile_manager_->user_data_dir().Append(chrome::kLocalStateFilename),
      std::move(pending_commit_->persistence_witnesses),
      base::BindOnce(&DormantTargetState::OnCommitOutcomeReadBack,
                     weak_factory_.GetWeakPtr()));
}

void BrowserProfilesRestoreLifecycle::DormantTargetState::
    OnCommitOutcomeReadBack(bool matches) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!pending_commit_) {
    return;
  }
  const bool durable =
      matches && pending_commit_->outcome_record && HasExactLiveCustody() &&
      JournalEndsWithExactFact(local_state_, resolved_target_.reservation_id,
                               *pending_commit_->outcome_record);
  if (!durable && !pending_commit_->dispatched) {
    FinishCommit(base::unexpected(TargetError::kStorageUnavailable));
    return;
  }
  ProfileBackupRestoreCommitObservation observation;
  if (durable) {
    observation.outcome = pending_commit_->observed_outcome;
    observation.journal_durable = true;
  }
  FinishCommit(std::move(observation));
}

void BrowserProfilesRestoreLifecycle::DormantTargetState::FinishCommit(
    CommitResult result) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!pending_commit_) {
    return;
  }
  CommitCallback callback = std::move(pending_commit_->callback);
  pending_commit_.reset();
  std::move(callback).Run(std::move(result));
}

}  // namespace taffy

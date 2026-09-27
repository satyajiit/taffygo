// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <memory>
#include <utility>

#include "base/functional/bind.h"
#include "chrome/browser/profiles/nuke_profile_directory_utils.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/browser_thread.h"
#include "taffy/browser/android/browser_profiles_restore_discovery_android_internal.h"
#include "taffy/browser/android/browser_profiles_restore_resolution_android_internal.h"
#include "taffy/browser/backup_restore_recovery_journal.h"
#include "taffy/components/storage/browser/dormant_backup_restore_target_lease.h"
#include "taffy/components/storage/browser/dormant_backup_restore_target_reconciler.h"
#include "taffy/components/storage/browser/dormant_backup_restore_target_resolution.h"

namespace taffy {
namespace {

namespace mojom = core_service::mojom;
namespace discovery = restore_discovery_internal;
namespace resolution = restore_resolution_internal;
using DiscoveryError = BackupRestoreRestartDiscoveryError;
using PhysicalError = BackupRestoreRestartPhysicalError;
using PhysicalResult = BackupRestoreRestartPhysicalResult;
using PhysicalState = BackupRestoreRestartPhysicalState;
using Status = BackupRestoreRestartDiscoveryStatus;

PhysicalError ProjectReconcileError(
    storage::backup::DormantBackupRestoreReconcileError error) {
  using Error = storage::backup::DormantBackupRestoreReconcileError;
  switch (error) {
    case Error::kTargetBusy:
      return PhysicalError::kBusy;
    case Error::kSchemaMismatch:
      return PhysicalError::kSchemaMismatch;
    case Error::kStorageUnavailable:
      return PhysicalError::kStorageUnavailable;
    case Error::kInvalidTarget:
    case Error::kTargetChanged:
    case Error::kInvalidWitness:
      return PhysicalError::kCustodyAmbiguous;
  }
  return PhysicalError::kStorageUnavailable;
}

PhysicalError ProjectResolutionError(
    storage::backup::DormantBackupRestoreResolutionError error) {
  using Error = storage::backup::DormantBackupRestoreResolutionError;
  switch (error) {
    case Error::kTargetBusy:
      return PhysicalError::kBusy;
    case Error::kSchemaMismatch:
      return PhysicalError::kSchemaMismatch;
    case Error::kStorageUnavailable:
    case Error::kCleanupFailed:
      return PhysicalError::kStorageUnavailable;
    case Error::kInvalidTarget:
    case Error::kTargetChanged:
    case Error::kInvalidWitness:
    case Error::kCandidateMismatch:
    case Error::kStageMismatch:
      return PhysicalError::kCustodyAmbiguous;
  }
  return PhysicalError::kStorageUnavailable;
}

base::FilePath DatabasePath(const base::FilePath& profile_path) {
  return profile_path.AppendASCII("TaffyCore").AppendASCII("core.sqlite3");
}

BackupRestoreRestartDiscoveryResult StatusOnly(Status status) {
  return BackupRestoreRestartDiscovery{.status = status};
}

mojom::BackupRestoreObservedOutcome CommitOutcome(PhysicalState state) {
  switch (state) {
    case PhysicalState::kPristine:
      return mojom::BackupRestoreObservedOutcome::kDefinitelyNotCompleted;
    case PhysicalState::kCommitted:
      return mojom::BackupRestoreObservedOutcome::kCompleted;
    case PhysicalState::kOutcomeUnknown:
      return mojom::BackupRestoreObservedOutcome::kOutcomeUnknown;
    case PhysicalState::kSchemaExact:
    case PhysicalState::kVerifiedDeleted:
      break;
  }
  return mojom::BackupRestoreObservedOutcome::kOutcomeUnknown;
}

}  // namespace

class BackupRestoreRestartDiscoveryOperation::BlockingOwner::StorageOwner {
 public:
  std::unique_ptr<storage::backup::DormantBackupRestoreTargetLease>
      absent_target_lease;
};

BackupRestoreRestartDiscoveryOperation::BlockingOwner::BlockingOwner()
    : storage_owner_(std::make_unique<StorageOwner>()) {}

BackupRestoreRestartDiscoveryOperation::BlockingOwner::~BlockingOwner() =
    default;

PhysicalResult
BackupRestoreRestartDiscoveryOperation::BlockingOwner::ReconcileCommit(
    base::FilePath target_profile_path,
    mojom::BackupRestoreTargetPtr exact_target,
    mojom::BackupRestoreCandidateWitnessPtr witness) {
  auto opened =
      storage::backup::DormantBackupRestoreTargetReconciler::OpenForRecovery(
          target_profile_path, std::move(exact_target));
  // base::expected has no operator bool when its value type converts to bool
  // on its own, which both std::unique_ptr and mojo::StructPtr do. Ask for the
  // value directly rather than testing the expected.
  if (!opened.has_value()) {
    return base::unexpected(ProjectReconcileError(opened.error()));
  }
  auto reconciled = (*opened)->Reconcile(std::move(witness));
  if (!reconciled) {
    return base::unexpected(ProjectReconcileError(reconciled.error()));
  }
  switch (*reconciled) {
    case storage::backup::DormantBackupRestoreReconcileState::kPristine:
      return PhysicalState::kPristine;
    case storage::backup::DormantBackupRestoreReconcileState::kCommitted:
      return PhysicalState::kCommitted;
    case storage::backup::DormantBackupRestoreReconcileState::kOutcomeUnknown:
      return PhysicalState::kOutcomeUnknown;
  }
  return base::unexpected(PhysicalError::kStorageUnavailable);
}

PhysicalResult
BackupRestoreRestartDiscoveryOperation::BlockingOwner::ObserveResolution(
    base::FilePath target_profile_path,
    mojom::BackupRestoreTargetPtr exact_target,
    mojom::BackupRestoreResolutionChoice choice) {
  if (storage_owner_->absent_target_lease) {
    return choice == mojom::BackupRestoreResolutionChoice::kDiscardCandidate &&
                   VerifyProfileAndCacheDirectoryDeletion(target_profile_path)
               ? PhysicalResult(PhysicalState::kVerifiedDeleted)
               : PhysicalResult(
                     base::unexpected(PhysicalError::kCustodyAmbiguous));
  }
  if (choice == mojom::BackupRestoreResolutionChoice::kAcceptCandidate) {
    auto verified = storage::backup::VerifyPublishedDormantBackupRestoreTarget(
        target_profile_path, std::move(exact_target));
    return verified ? PhysicalResult(PhysicalState::kSchemaExact)
                    : PhysicalResult(base::unexpected(
                          ProjectResolutionError(verified.error())));
  }
  if (choice != mojom::BackupRestoreResolutionChoice::kDiscardCandidate) {
    return base::unexpected(PhysicalError::kCustodyAmbiguous);
  }
  storage_owner_->absent_target_lease =
      storage::backup::DormantBackupRestoreTargetLease::
          TryAcquireForAbsentTarget(DatabasePath(target_profile_path));
  if (!storage_owner_->absent_target_lease ||
      !VerifyProfileAndCacheDirectoryDeletion(target_profile_path)) {
    storage_owner_->absent_target_lease.reset();
    return PhysicalState::kOutcomeUnknown;
  }
  return PhysicalState::kVerifiedDeleted;
}

bool BackupRestoreRestartDiscoveryOperation::BlockingOwner::Close() {
  storage_owner_.reset();
  storage_owner_ = std::make_unique<StorageOwner>();
  return true;
}

void BackupRestoreRestartDiscoveryOperation::BeginCommitReconciliation() {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!classification_ || !classification_->reconciliation ||
      classification_->reconciliation->intent !=
          mojom::BackupRestorePhysicalIntent::kCommitCandidate ||
      records_.empty() || !records_.front() || !records_.front()->intent ||
      !records_.front()->binding ||
      records_.front()->intent->intent_id !=
          classification_->reconciliation->intent_id) {
    Finish(StatusOnly(Status::kCustodyAmbiguous));
    return;
  }
  active_intent_ = records_.front().Clone();
  probing_intent_ = mojom::BackupRestorePhysicalIntent::kCommitCandidate;
  physical_observation_attempted_ = true;
  auto witness = mojom::BackupRestoreCandidateWitness::New();
  witness->selection = records_.front()->binding->selection;
  witness->record_count = records_.front()->binding->record_count;
  witness->candidate_records_sha256 =
      records_.front()->binding->candidate_records_sha256;
  blocking_owner_.AsyncCall(&BlockingOwner::ReconcileCommit)
      .WithArgs(target_.target_profile_path,
                mojom::BackupRestoreTarget::New(
                    mojom::BackupRestoreTargetKind::kNewRegularProfile,
                    target_.target_profile_id),
                std::move(witness))
      .Then(base::BindOnce(
          &BackupRestoreRestartDiscoveryOperation::OnPhysicalProbe,
          weak_factory_.GetWeakPtr()));
}

void BackupRestoreRestartDiscoveryOperation::BeginResolutionObservation(
    mojom::BackupRestorePhysicalIntent intent) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!classification_ || !classification_->reconciliation ||
      (intent != mojom::BackupRestorePhysicalIntent::kAcceptCandidate &&
       intent != mojom::BackupRestorePhysicalIntent::kDiscardCandidate)) {
    Finish(StatusOnly(Status::kCustodyAmbiguous));
    return;
  }
  for (const auto& record : records_) {
    if (record && record->intent &&
        record->intent->intent_id ==
            classification_->reconciliation->intent_id &&
        record->intent->intent == intent) {
      active_intent_ = record.Clone();
      break;
    }
  }
  if (!active_intent_) {
    Finish(StatusOnly(Status::kCustodyAmbiguous));
    return;
  }
  probing_intent_ = intent;
  physical_observation_attempted_ = true;
  const auto choice =
      intent == mojom::BackupRestorePhysicalIntent::kAcceptCandidate
          ? mojom::BackupRestoreResolutionChoice::kAcceptCandidate
          : mojom::BackupRestoreResolutionChoice::kDiscardCandidate;
  blocking_owner_.AsyncCall(&BlockingOwner::ObserveResolution)
      .WithArgs(target_.target_profile_path,
                mojom::BackupRestoreTarget::New(
                    mojom::BackupRestoreTargetKind::kNewRegularProfile,
                    target_.target_profile_id),
                choice)
      .Then(base::BindOnce(
          &BackupRestoreRestartDiscoveryOperation::OnPhysicalProbe,
          weak_factory_.GetWeakPtr()));
}

void BackupRestoreRestartDiscoveryOperation::OnPhysicalProbe(
    PhysicalResult result) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!result) {
    Finish(discovery::ProjectPhysicalFailure(result.error()));
    return;
  }
  if (!probing_intent_ || !ExactCurrentState()) {
    Finish(StatusOnly(Status::kCustodyAmbiguous));
    return;
  }
  mojom::BackupRestoreObservedOutcome outcome =
      mojom::BackupRestoreObservedOutcome::kOutcomeUnknown;
  switch (*probing_intent_) {
    case mojom::BackupRestorePhysicalIntent::kCommitCandidate:
      outcome = CommitOutcome(*result);
      break;
    case mojom::BackupRestorePhysicalIntent::kAcceptCandidate:
      if (*result == PhysicalState::kSchemaExact &&
          TargetIsPublishedAndCoreless()) {
        outcome = mojom::BackupRestoreObservedOutcome::kCompleted;
      } else if (*result == PhysicalState::kSchemaExact &&
                 TargetIsHiddenAndCoreless()) {
        outcome = mojom::BackupRestoreObservedOutcome::kDefinitelyNotCompleted;
      }
      break;
    case mojom::BackupRestorePhysicalIntent::kDiscardCandidate:
      if (*result == PhysicalState::kVerifiedDeleted &&
          TargetIsVerifiedDeleted()) {
        outcome = mojom::BackupRestoreObservedOutcome::kCompleted;
      }
      break;
  }
  if (outcome == mojom::BackupRestoreObservedOutcome::kOutcomeUnknown &&
      HistoryHasUnknownForActiveIntent()) {
    Finish(StatusOnly(Status::kOutcomeUnknown));
    return;
  }
  PersistObservedOutcome(outcome);
}

void BackupRestoreRestartDiscoveryOperation::PersistObservedOutcome(
    mojom::BackupRestoreObservedOutcome outcome) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!active_intent_ || !active_intent_->intent || !probing_intent_ ||
      !ExactCurrentState()) {
    Finish(StatusOnly(Status::kCustodyAmbiguous));
    return;
  }
  BackupRestoreRecoveryRecordResult appended =
      base::unexpected(BackupRestoreProfileRegistryError::kWrongPhysicalState);
  if (*probing_intent_ ==
      mojom::BackupRestorePhysicalIntent::kCommitCandidate) {
    mojom::BackupRestoreCommitOutcome commit =
        mojom::BackupRestoreCommitOutcome::kOutcomeUnknown;
    if (outcome == mojom::BackupRestoreObservedOutcome::kCompleted) {
      commit = mojom::BackupRestoreCommitOutcome::kCommitted;
    } else if (outcome ==
               mojom::BackupRestoreObservedOutcome::kDefinitelyNotCompleted) {
      commit = mojom::BackupRestoreCommitOutcome::kDefinitelyNotCommitted;
    }
    appended = RecordBackupRestoreCommitOutcome(
        local_state_, reservation_.reservation_id, *active_intent_, commit);
  } else {
    mojom::BackupRestoreResolutionOutcome resolution_outcome =
        mojom::BackupRestoreResolutionOutcome::kOutcomeUnknown;
    if (outcome == mojom::BackupRestoreObservedOutcome::kCompleted) {
      resolution_outcome = mojom::BackupRestoreResolutionOutcome::kCompleted;
    } else if (outcome ==
               mojom::BackupRestoreObservedOutcome::kDefinitelyNotCompleted) {
      resolution_outcome =
          mojom::BackupRestoreResolutionOutcome::kDefinitelyNotCompleted;
    }
    appended = RecordBackupRestoreResolutionOutcome(
        local_state_, reservation_.reservation_id, *active_intent_,
        resolution_outcome);
  }
  if (!appended.has_value()) {
    Finish(StatusOnly(Status::kCustodyAmbiguous));
    return;
  }
  outcome_record_ = std::move(*appended);
  auto records = ReadBackupRestoreRecoveryJournal(local_state_,
                                                  reservation_.reservation_id);
  if (!records || records->empty() || !records->back() ||
      !records->back()->Equals(*outcome_record_)) {
    Finish(StatusOnly(Status::kCustodyAmbiguous));
    return;
  }
  records_ = std::move(*records);
  persistence_witnesses_ = resolution::CaptureResolutionWitnesses(
      *local_state_, target_.target_profile_path, false);
  if (persistence_witnesses_.empty()) {
    Finish(base::unexpected(DiscoveryError::kStorageUnavailable));
    return;
  }
  local_state_->CommitPendingWrite(base::BindOnce(
      &BackupRestoreRestartDiscoveryOperation::OnOutcomeWriteDrained,
      weak_factory_.GetWeakPtr()));
}

void BackupRestoreRestartDiscoveryOperation::OnOutcomeWriteDrained() {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  resolution::VerifyResolutionPreferences(
      profile_manager_, std::move(persistence_witnesses_),
      base::BindOnce(&BackupRestoreRestartDiscoveryOperation::OnOutcomeReadBack,
                     weak_factory_.GetWeakPtr()));
}

void BackupRestoreRestartDiscoveryOperation::OnOutcomeReadBack(bool matches) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  auto records = ReadBackupRestoreRecoveryJournal(local_state_,
                                                  reservation_.reservation_id);
  if (!matches || !outcome_record_ || !records || records->empty() ||
      !records->back() || !records->back()->Equals(*outcome_record_) ||
      !ExactCurrentState()) {
    Finish(StatusOnly(Status::kCustodyAmbiguous));
    return;
  }
  InspectCurrentHistory();
}

bool BackupRestoreRestartDiscoveryOperation::HistoryHasUnknownForActiveIntent()
    const {
  if (!active_intent_ || !active_intent_->intent) {
    return false;
  }
  for (const auto& record : records_) {
    if (record && record->outcome &&
        record->outcome->intent_id == active_intent_->intent->intent_id &&
        record->outcome->outcome ==
            mojom::BackupRestoreObservedOutcome::kOutcomeUnknown) {
      return true;
    }
  }
  return false;
}

}  // namespace taffy

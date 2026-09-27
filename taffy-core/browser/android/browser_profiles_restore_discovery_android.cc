// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <memory>
#include <utility>

#include "base/functional/bind.h"
#include "base/task/task_traits.h"
#include "base/task/thread_pool.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_attributes_entry.h"
#include "chrome/browser/profiles/profile_attributes_storage.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/browser_thread.h"
#include "taffy/browser/android/browser_profiles_restore_discovery_android_internal.h"
#include "taffy/browser/android/browser_profiles_restore_resolution_android_internal.h"
#include "taffy/browser/backup_restore_recovery_journal.h"
#include "taffy/browser/backup_restore_reservation_retirement.h"
#include "taffy/browser/core_backup_protocol.h"
#include "taffy/browser/core_backup_recovery_validation.h"
#include "taffy/browser/core_service_manager.h"
#include "taffy/browser/core_service_manager_factory.h"

namespace taffy {
namespace {

// `taffy::mojom` (the BIP page contract) is visible here through the backup
// protocol headers, and an anonymous-namespace alias named `mojom` is
// nominated into `taffy` too, so the short name would be ambiguous at every
// use. Name the Core Service contract explicitly instead.
namespace core_mojom = core_service::mojom;
namespace discovery = restore_discovery_internal;
namespace reconciliation = restore_reconciliation_internal;
namespace resolution = restore_resolution_internal;
using ClassificationKind = core_mojom::BackupRestoreRecoveryClassificationKind;
using Error = BackupRestoreRestartDiscoveryError;
using InspectionStatus = core_mojom::BackupRestoreRecoveryInspectionStatus;
using Quarantine = BackupRestoreProfileQuarantineStatus;
using Result = BackupRestoreRestartDiscoveryResult;
using Status = BackupRestoreRestartDiscoveryStatus;

Result StatusOnly(Status status) {
  return BackupRestoreRestartDiscovery{.status = status};
}

Result RegistryFailure(BackupRestoreProfileRegistryError error) {
  return error == BackupRestoreProfileRegistryError::kUnavailable
             ? Result(base::unexpected(Error::kStorageUnavailable))
             : StatusOnly(Status::kCustodyAmbiguous);
}

BackupRestoreRecoveryRecords CloneHistory(
    const BackupRestoreRecoveryRecords& records) {
  BackupRestoreRecoveryRecords copy;
  copy.reserve(records.size());
  for (const auto& record : records) {
    copy.push_back(record.Clone());
  }
  return copy;
}

bool IsValidMode(BackupRestoreRestartDiscoveryMode mode) {
  switch (mode) {
    case BackupRestoreRestartDiscoveryMode::kInspectOnly:
    case BackupRestoreRestartDiscoveryMode::kReconcileCommit:
      return true;
  }
  return false;
}

}  // namespace

BackupRestoreRestartDiscoveryOperation::BackupRestoreRestartDiscoveryOperation(
    ProfileManager* profile_manager,
    PrefService* local_state,
    base::FilePath requested_source_path,
    BackupRestoreRestartDiscoveryMode mode,
    CompletionCallback completion)
    : profile_manager_(profile_manager),
      local_state_(local_state),
      requested_source_path_(std::move(requested_source_path)),
      mode_(mode),
      completion_(std::move(completion)),
      blocking_owner_(base::ThreadPool::CreateSequencedTaskRunner(
          {base::MayBlock(), base::TaskPriority::USER_VISIBLE,
           base::TaskShutdownBehavior::BLOCK_SHUTDOWN})) {}

BackupRestoreRestartDiscoveryOperation::
    ~BackupRestoreRestartDiscoveryOperation() = default;

void BackupRestoreRestartDiscoveryOperation::Start() {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!profile_manager_ || !local_state_ || !completion_ ||
      requested_source_path_.empty() ||
      requested_source_path_.DirName() != profile_manager_->user_data_dir() ||
      !profile_manager_->IsAllowedProfilePath(requested_source_path_) ||
      !IsValidMode(mode_)) {
    Finish(base::unexpected(Error::kInvalidArgument));
    return;
  }
  auto reservations = ReadBackupRestoreProfileReservations(local_state_);
  if (!reservations) {
    Finish(RegistryFailure(reservations.error()));
    return;
  }
  if (reservations->empty()) {
    Finish(StatusOnly(Status::kNone));
    return;
  }
  if (reservations->size() != 1u) {
    Finish(StatusOnly(Status::kCustodyAmbiguous));
    return;
  }
  reservation_ = reservations->front();
  const base::FilePath source_path = profile_manager_->user_data_dir().Append(
      reservation_.source_profile_base_name);
  if (source_path != requested_source_path_) {
    // Do not read or return the target label for a different source profile.
    Finish(StatusOnly(Status::kSourceUnavailable));
    return;
  }
  Profile* const source =
      profile_manager_->GetProfileByPath(requested_source_path_);
  ProfileAttributesEntry* const source_entry =
      profile_manager_->GetProfileAttributesStorage()
          .GetProfileAttributesWithPath(requested_source_path_);
  if (!source || source->IsOffTheRecord() ||
      !profile_manager_->IsValidProfile(source) || !source_entry ||
      source_entry->IsOmitted() || source_entry->IsEphemeral() ||
      profile_manager_->GetLastUsedProfileDir() != requested_source_path_ ||
      BackupRestoreQuarantineForProfilePath(local_state_,
                                            requested_source_path_) !=
          Quarantine::kNotQuarantined) {
    Finish(StatusOnly(Status::kSourceUnavailable));
    return;
  }
  if (reservation_.physical_state !=
          BackupRestoreProfilePhysicalState::kProfileCreated ||
      !reservation_.target_profile_id) {
    Finish(StatusOnly(Status::kPrecommit));
    return;
  }
  auto records = ReadBackupRestoreRecoveryJournal(local_state_,
                                                  reservation_.reservation_id);
  if (!records) {
    Finish(RegistryFailure(records.error()));
    return;
  }
  if (records->empty()) {
    Finish(StatusOnly(Status::kPrecommit));
    return;
  }
  auto presentation = ReadBackupRestoreRecoveryPresentation(
      local_state_, reservation_.reservation_id);
  if (!presentation) {
    Finish(presentation.error() == BackupRestoreProfileRegistryError::kNotFound
               ? StatusOnly(Status::kPresentationUnavailable)
               : RegistryFailure(presentation.error()));
    return;
  }
  auto target = resolution::ResolveTarget(profile_manager_, local_state_,
                                          reservation_.reservation_id);
  if (!target) {
    Finish(StatusOnly(Status::kCustodyAmbiguous));
    return;
  }
  auto exact_reservation = resolution::ExactReservation(local_state_, *target);
  if (!exact_reservation || *exact_reservation != reservation_) {
    Finish(StatusOnly(Status::kCustodyAmbiguous));
    return;
  }
  CoreServiceManager* const manager =
      CoreServiceManagerFactory::GetForProfileIfExists(source);
  if (!manager ||
      manager->availability() != CoreServiceManager::Availability::kReady) {
    Finish(StatusOnly(Status::kSourceUnavailable));
    return;
  }
  if (presentation->source_profile_id != manager->browser_profile_id() ||
      records->empty() || !records->front() || !records->front()->binding ||
      records->front()->binding->owner_profile_id !=
          manager->browser_profile_id()) {
    Finish(StatusOnly(Status::kCustodyAmbiguous));
    return;
  }
  target_ = std::move(*target);
  presentation_ = std::move(*presentation);
  records_ = std::move(*records);
  persistence_witnesses_ = resolution::CaptureResolutionWitnesses(
      *local_state_, target_.target_profile_path, false);
  if (persistence_witnesses_.empty()) {
    Finish(base::unexpected(Error::kStorageUnavailable));
    return;
  }
  resolution::VerifyResolutionPreferences(
      profile_manager_, std::move(persistence_witnesses_),
      base::BindOnce(
          &BackupRestoreRestartDiscoveryOperation::OnPreferencesVerified,
          weak_factory_.GetWeakPtr()));
}

void BackupRestoreRestartDiscoveryOperation::OnPreferencesVerified(
    bool matches) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!matches || !ExactCurrentState()) {
    Finish(StatusOnly(Status::kCustodyAmbiguous));
    return;
  }
  InspectCurrentHistory();
}

void BackupRestoreRestartDiscoveryOperation::InspectCurrentHistory() {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!ExactCurrentState()) {
    Finish(StatusOnly(Status::kCustodyAmbiguous));
    return;
  }
  CoreServiceManager* const manager =
      reconciliation::LiveSourceManager(profile_manager_, target_, records_);
  inspection_operation_ =
      manager
          ? resolution::NewOperation(manager->service_generation(), "discover")
          : nullptr;
  if (!manager || !inspection_operation_) {
    Finish(StatusOnly(Status::kSourceUnavailable));
    return;
  }
  manager->backup_protocol().InspectBackupRestoreRecovery(
      core_mojom::BackupRestoreRecoveryInspectionRequest::New(
          inspection_operation_.Clone(), CloneHistory(records_)),
      base::BindOnce(
          &BackupRestoreRestartDiscoveryOperation::OnHistoryInspected,
          weak_factory_.GetWeakPtr()));
}

void BackupRestoreRestartDiscoveryOperation::OnHistoryInspected(
    core_mojom::BackupRestoreRecoveryInspectionResultPtr result) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!ExactCurrentState()) {
    Finish(StatusOnly(Status::kCustodyAmbiguous));
    return;
  }
  if (!reconciliation::ValidInspectionResult(inspection_operation_.get(),
                                             result.get()) ||
      !result->classification) {
    Finish(result && result->status == InspectionStatus::kUnavailable
               ? StatusOnly(Status::kSourceUnavailable)
               : StatusOnly(Status::kCustodyAmbiguous));
    return;
  }
  classification_ = std::move(result->classification);
  HandleClassification();
}

void BackupRestoreRestartDiscoveryOperation::HandleClassification() {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!classification_ || !presentation_ || !ExactCurrentState()) {
    Finish(StatusOnly(Status::kCustodyAmbiguous));
    return;
  }
  switch (classification_->kind) {
    case ClassificationKind::kRollbackAvailable:
    case ClassificationKind::kCleanupRequired: {
      if (classification_->reconciliation || !TargetIsHiddenAndCoreless()) {
        Finish(StatusOnly(Status::kCustodyAmbiguous));
        return;
      }
      const bool rollback =
          classification_->kind == ClassificationKind::kRollbackAvailable;
      BackupRestoreRestartDiscovery discovered{
          .status =
              rollback ? Status::kRollbackAvailable : Status::kCleanupRequired,
          .candidate = BackupRestoreRestartCandidate{
              .reservation_id = reservation_.reservation_id,
              .presentation = *presentation_,
              .action =
                  rollback
                      ? BackupRestoreRestartCandidateAction::kReview
                      : BackupRestoreRestartCandidateAction::kDiscardOnly}};
      Finish(discovery::IsWellFormedDiscovery(discovered)
                 ? Result(std::move(discovered))
                 : StatusOnly(Status::kCustodyAmbiguous));
      return;
    }
    case ClassificationKind::kReconcileRequired:
      break;
    case ClassificationKind::kPublished:
      if (classification_->reconciliation) {
        Finish(StatusOnly(Status::kCustodyAmbiguous));
        return;
      }
      BeginTerminalVerification(
          Status::kPublished,
          core_mojom::BackupRestoreResolutionChoice::kAcceptCandidate);
      return;
    case ClassificationKind::kVerifiedDeleted:
      if (classification_->reconciliation) {
        Finish(StatusOnly(Status::kCustodyAmbiguous));
        return;
      }
      BeginTerminalVerification(
          Status::kVerifiedDeleted,
          core_mojom::BackupRestoreResolutionChoice::kDiscardCandidate);
      return;
  }
  if (!classification_->reconciliation || physical_observation_attempted_) {
    Finish(StatusOnly(Status::kOutcomeUnknown));
    return;
  }
  const core_mojom::BackupRestorePhysicalIntent intent =
      classification_->reconciliation->intent;
  if (intent == core_mojom::BackupRestorePhysicalIntent::kCommitCandidate) {
    if (mode_ == BackupRestoreRestartDiscoveryMode::kInspectOnly) {
      Finish(StatusOnly(Status::kOutcomeUnknown));
      return;
    }
    BeginCommitReconciliation();
    return;
  }
  BeginResolutionObservation(intent);
}

}  // namespace taffy

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
#include "base/uuid.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_attributes_entry.h"
#include "chrome/browser/profiles/profile_attributes_storage.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/browser_thread.h"
#include "taffy/browser/android/browser_profiles_restore_resolution_android_internal.h"
#include "taffy/browser/backup_restore_recovery_journal.h"
#include "taffy/browser/backup_restore_reservation_retirement.h"
#include "taffy/browser/core_backup_planning_validation.h"
#include "taffy/browser/core_backup_protocol.h"
#include "taffy/browser/core_backup_recovery_validation.h"
#include "taffy/browser/core_service_manager.h"
#include "taffy/browser/core_service_manager_factory.h"

namespace taffy {
namespace {

// `taffy::mojom` -- the BIP page-semantics contract -- is also visible in
// this translation unit, so a plain `mojom` alias would be ambiguous at
// `taffy` scope below. The core-service contract is reached by this name.
namespace core_mojom = core_service::mojom;
namespace resolution = restore_resolution_internal;
using Error = BackupRestoreCandidateResolutionError;

BackupRestoreRecoveryRecords CloneHistory(
    const BackupRestoreRecoveryRecords& records) {
  BackupRestoreRecoveryRecords copy;
  copy.reserve(records.size());
  for (const auto& record : records) {
    copy.push_back(record.Clone());
  }
  return copy;
}

CoreServiceManager* LiveManager(
    ProfileManager* profile_manager,
    const ResolvedDormantBackupRestoreTarget& target,
    const BackupRestoreRecoveryRecords& records) {
  return restore_reconciliation_internal::LiveSourceManager(profile_manager,
                                                            target, records);
}

bool TargetRemainsHidden(ProfileManager* profile_manager,
                         const base::FilePath& target_path) {
  ProfileAttributesEntry* const entry =
      profile_manager->GetProfileAttributesStorage()
          .GetProfileAttributesWithPath(target_path);
  Profile* const target = profile_manager->GetProfileByPath(target_path);
  return entry && entry->IsEphemeral() &&
         (!target ||
          !CoreServiceManagerFactory::HasExistingInstanceForProfile(target));
}

bool IsTerminalForChoice(
    const core_mojom::BackupRestoreRecoveryClassification& classification,
    core_mojom::BackupRestoreResolutionChoice choice) {
  using Choice = core_mojom::BackupRestoreResolutionChoice;
  using Kind = core_mojom::BackupRestoreRecoveryClassificationKind;
  return (choice == Choice::kAcceptCandidate &&
          classification.kind == Kind::kPublished) ||
         (choice == Choice::kDiscardCandidate &&
          classification.kind == Kind::kVerifiedDeleted);
}

}  // namespace

BrowserProfilesRestoreLifecycle::PendingCandidateResolution::
    PendingCandidateResolution()
    : blocking_owner(base::ThreadPool::CreateSequencedTaskRunner(
          {base::MayBlock(), base::TaskPriority::USER_BLOCKING,
           base::TaskShutdownBehavior::BLOCK_SHUTDOWN})) {}

BrowserProfilesRestoreLifecycle::PendingCandidateResolution::
    ~PendingCandidateResolution() = default;

void BrowserProfilesRestoreLifecycle::ResolveBackupRestoreCandidate(
    std::string reservation_id,
    core_mojom::BackupRestoreResolutionChoice choice,
    CandidateResolutionCallback callback) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!callback) {
    return;
  }
  if (choice != core_mojom::BackupRestoreResolutionChoice::kAcceptCandidate &&
      choice != core_mojom::BackupRestoreResolutionChoice::kDiscardCandidate) {
    std::move(callback).Run(base::unexpected(Error::kInvalidArgument));
    return;
  }
  if (operation_in_flight()) {
    std::move(callback).Run(base::unexpected(Error::kBusy));
    return;
  }
  auto target =
      resolution::ResolveTarget(profile_manager_, local_state_, reservation_id);
  if (!target) {
    std::move(callback).Run(base::unexpected(target.error()));
    return;
  }
  auto reservation = resolution::ExactReservation(local_state_, *target);
  auto records = ReadBackupRestoreRecoveryJournal(local_state_, reservation_id);
  if (!reservation || !records || records->empty()) {
    std::move(callback).Run(base::unexpected(
        !reservation ? reservation.error() : Error::kRegistryRefused));
    return;
  }
  CoreServiceManager* const manager =
      LiveManager(profile_manager_, *target, *records);
  if (!manager) {
    std::move(callback).Run(base::unexpected(Error::kCoreUnavailable));
    return;
  }
  pending_candidate_resolution_ =
      std::make_unique<PendingCandidateResolution>();
  auto& pending = *pending_candidate_resolution_;
  pending.target = std::move(*target);
  pending.reservation = std::move(*reservation);
  pending.records = std::move(*records);
  pending.choice = choice;
  pending.callback = std::move(callback);
  pending.persistence_witnesses = resolution::CaptureResolutionWitnesses(
      *local_state_, pending.target.target_profile_path, false);
  resolution::VerifyResolutionPreferences(
      profile_manager_, std::move(pending.persistence_witnesses),
      base::BindOnce(
          &BrowserProfilesRestoreLifecycle::OnCandidateResolutionReadBack,
          weak_factory_.GetWeakPtr()));
}

void BrowserProfilesRestoreLifecycle::OnCandidateResolutionReadBack(
    bool matches) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!pending_candidate_resolution_) {
    return;
  }
  auto& pending = *pending_candidate_resolution_;
  if (!matches || !resolution::ExactCurrentState(
                      profile_manager_, local_state_, pending.target,
                      pending.reservation, pending.records)) {
    FinishCandidateResolution(base::unexpected(Error::kPersistenceFailed));
    return;
  }
  CoreServiceManager* const manager =
      LiveManager(profile_manager_, pending.target, pending.records);
  pending.operation = manager ? resolution::NewOperation(
                                    manager->service_generation(), "inspect")
                              : nullptr;
  if (!manager || !pending.operation) {
    FinishCandidateResolution(base::unexpected(Error::kCoreUnavailable));
    return;
  }
  manager->backup_protocol().InspectBackupRestoreRecovery(
      core_mojom::BackupRestoreRecoveryInspectionRequest::New(
          pending.operation.Clone(), CloneHistory(pending.records)),
      base::BindOnce(
          &BrowserProfilesRestoreLifecycle::OnCandidateResolutionInspected,
          weak_factory_.GetWeakPtr()));
}

void BrowserProfilesRestoreLifecycle::OnCandidateResolutionInspected(
    core_mojom::BackupRestoreRecoveryInspectionResultPtr result) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!pending_candidate_resolution_) {
    return;
  }
  auto& pending = *pending_candidate_resolution_;
  if (!restore_reconciliation_internal::ValidInspectionResult(
          pending.operation.get(), result.get()) ||
      !result->classification ||
      !resolution::ExactCurrentState(profile_manager_, local_state_,
                                     pending.target, pending.reservation,
                                     pending.records)) {
    FinishCandidateResolution(base::unexpected(Error::kHistoryRefused));
    return;
  }
  pending.classification = std::move(result->classification);
  if (IsTerminalForChoice(*pending.classification, pending.choice)) {
    BeginCandidateResolutionRetirement();
    return;
  }
  if (pending.classification->kind ==
      core_mojom::BackupRestoreRecoveryClassificationKind::kReconcileRequired) {
    if (!pending.classification->reconciliation ||
        pending.classification->reconciliation->intent ==
            core_mojom::BackupRestorePhysicalIntent::kCommitCandidate) {
      FinishCandidateResolution(base::unexpected(Error::kHistoryRefused));
      return;
    }
    const bool matching =
        (pending.choice ==
             core_mojom::BackupRestoreResolutionChoice::kAcceptCandidate &&
         pending.classification->reconciliation->intent ==
             core_mojom::BackupRestorePhysicalIntent::kAcceptCandidate) ||
        (pending.choice ==
             core_mojom::BackupRestoreResolutionChoice::kDiscardCandidate &&
         pending.classification->reconciliation->intent ==
             core_mojom::BackupRestorePhysicalIntent::kDiscardCandidate);
    if (!matching) {
      FinishCandidateResolution(base::unexpected(Error::kHistoryRefused));
      return;
    }
    for (const auto& record : pending.records) {
      if (record && record->intent &&
          record->intent->intent_id ==
              pending.classification->reconciliation->intent_id &&
          record->intent->intent ==
              pending.classification->reconciliation->intent) {
        pending.intent = record.Clone();
        break;
      }
    }
    if (!pending.intent) {
      FinishCandidateResolution(base::unexpected(Error::kHistoryRefused));
      return;
    }
    // An outstanding/unknown resolution may only be observed, never replayed.
    pending.blocking_owner
        .AsyncCall(&CandidateResolutionBlockingOwner::VerifyTerminalPhysical)
        .WithArgs(pending.target.target_profile_path,
                  core_mojom::BackupRestoreTarget::New(
                      core_mojom::BackupRestoreTargetKind::kNewRegularProfile,
                      pending.target.target_profile_id),
                  pending.choice)
        .Then(base::BindOnce(&BrowserProfilesRestoreLifecycle::
                                 OnCandidateResolutionPhysicalRetirementChecked,
                             weak_factory_.GetWeakPtr()));
    return;
  }
  if (!TargetRemainsHidden(profile_manager_,
                           pending.target.target_profile_path)) {
    FinishCandidateResolution(base::unexpected(Error::kProfileStateRefused));
    return;
  }
  auto witness = resolution::WitnessFromHistory(pending.records);
  if (!witness) {
    FinishCandidateResolution(base::unexpected(Error::kHistoryRefused));
    return;
  }
  pending.blocking_owner.AsyncCall(&CandidateResolutionBlockingOwner::Prepare)
      .WithArgs(pending.target.target_profile_path,
                core_mojom::BackupRestoreTarget::New(
                    core_mojom::BackupRestoreTargetKind::kNewRegularProfile,
                    pending.target.target_profile_id),
                std::move(witness), pending.choice)
      .Then(base::BindOnce(&BrowserProfilesRestoreLifecycle::
                               OnCandidateResolutionStoragePrepared,
                           weak_factory_.GetWeakPtr()));
}

void BrowserProfilesRestoreLifecycle::OnCandidateResolutionStoragePrepared(
    base::expected<void, Error> result) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!pending_candidate_resolution_) {
    return;
  }
  if (!result) {
    FinishCandidateResolution(base::unexpected(result.error()));
    return;
  }
  auto& pending = *pending_candidate_resolution_;
  CoreServiceManager* const manager =
      resolution::ExactCurrentState(profile_manager_, local_state_,
                                    pending.target, pending.reservation,
                                    pending.records)
          ? LiveManager(profile_manager_, pending.target, pending.records)
          : nullptr;
  pending.operation = manager ? resolution::NewOperation(
                                    manager->service_generation(), "resolve")
                              : nullptr;
  const base::Uuid intent = base::Uuid::GenerateRandomV4();
  if (!manager || !pending.operation || !intent.is_valid() ||
      !TargetRemainsHidden(profile_manager_,
                           pending.target.target_profile_path)) {
    FinishCandidateResolution(base::unexpected(Error::kCoreUnavailable));
    return;
  }
  pending.intent_id = intent.AsLowercaseString();
  manager->backup_protocol().ChooseBackupRestoreRecoveryResolution(
      core_mojom::BackupRestoreRecoveryResolutionRequest::New(
          pending.operation.Clone(), CloneHistory(pending.records),
          pending.choice, pending.intent_id),
      base::BindOnce(
          &BrowserProfilesRestoreLifecycle::OnCandidateResolutionAuthorized,
          weak_factory_.GetWeakPtr()));
}

}  // namespace taffy

// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <utility>

#include "base/functional/bind.h"
#include "chrome/browser/profiles/nuke_profile_directory_utils.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_attributes_entry.h"
#include "chrome/browser/profiles/profile_attributes_storage.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/browser_thread.h"
#include "taffy/browser/android/browser_profiles_restore_resolution_android_internal.h"
#include "taffy/browser/backup_restore_recovery_journal.h"
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

bool JournalEndsWith(const PrefService* local_state,
                     const std::string& reservation_id,
                     const core_mojom::BackupRestoreRecoveryRecord& expected) {
  auto records = ReadBackupRestoreRecoveryJournal(local_state, reservation_id);
  return records && records->size() == expected.sequence && !records->empty() &&
         records->back() && records->back()->Equals(expected);
}

CoreServiceManager* CurrentManager(
    ProfileManager* profile_manager,
    PrefService* local_state,
    const ResolvedDormantBackupRestoreTarget& target,
    const BackupRestoreProfileReservation& reservation,
    const BackupRestoreRecoveryRecords& records) {
  if (!resolution::ExactCurrentState(profile_manager, local_state, target,
                                     reservation, records)) {
    return nullptr;
  }
  return restore_reconciliation_internal::LiveSourceManager(profile_manager,
                                                            target, records);
}

bool ExactResolutionAuthority(
    const core_mojom::BackupRestoreRecoveryResolutionAuthorization&
        authorization,
    const ResolvedDormantBackupRestoreTarget& target,
    const BackupRestoreRecoveryRecords& records,
    core_mojom::BackupRestoreResolutionChoice choice,
    const std::string& intent_id,
    const CoreServiceManager& manager) {
  return authorization.binding &&
         authorization.binding->reservation_id == target.reservation_id &&
         authorization.binding->target_kind ==
             core_mojom::BackupRestoreTargetKind::kNewRegularProfile &&
         authorization.binding->target_profile_id == target.target_profile_id &&
         authorization.choice == choice &&
         authorization.intent_id == intent_id &&
         restore_reconciliation_internal::ExactHistory(
             authorization.history_prefix, records) &&
         IsLiveBackupRestoreRecoveryResolutionAuthorization(
             &authorization, manager.service_generation(),
             BackupPlanningNowMonotonicMillis(), manager.browser_profile_id());
}

bool HistoryHasAuthorizedIntent(
    const core_mojom::BackupRestoreRecoveryResolutionAuthorization&
        authorization,
    const BackupRestoreRecoveryRecords& records,
    const core_mojom::BackupRestoreRecoveryRecord* intent) {
  if (!intent || records.size() != authorization.history_prefix.size() + 1u ||
      !records.back() || !records.back()->Equals(*intent)) {
    return false;
  }
  for (size_t index = 0; index < authorization.history_prefix.size(); ++index) {
    if (!authorization.history_prefix[index] || !records[index] ||
        !authorization.history_prefix[index]->Equals(*records[index])) {
      return false;
    }
  }
  return true;
}

bool LiveResolutionAuthority(
    const core_mojom::BackupRestoreRecoveryResolutionAuthorization&
        authorization,
    const ResolvedDormantBackupRestoreTarget& target,
    core_mojom::BackupRestoreResolutionChoice choice,
    const std::string& intent_id,
    const CoreServiceManager& manager) {
  return authorization.binding &&
         authorization.binding->reservation_id == target.reservation_id &&
         authorization.binding->target_kind ==
             core_mojom::BackupRestoreTargetKind::kNewRegularProfile &&
         authorization.binding->target_profile_id == target.target_profile_id &&
         authorization.choice == choice &&
         authorization.intent_id == intent_id &&
         IsLiveBackupRestoreRecoveryResolutionAuthorization(
             &authorization, manager.service_generation(),
             BackupPlanningNowMonotonicMillis(), manager.browser_profile_id());
}

bool TargetIsHiddenAndCoreless(ProfileManager* profile_manager,
                               const base::FilePath& target_path) {
  ProfileAttributesEntry* const entry =
      profile_manager->GetProfileAttributesStorage()
          .GetProfileAttributesWithPath(target_path);
  Profile* const profile = profile_manager->GetProfileByPath(target_path);
  return entry && entry->IsEphemeral() &&
         (!profile ||
          !CoreServiceManagerFactory::HasExistingInstanceForProfile(profile));
}

}  // namespace

void BrowserProfilesRestoreLifecycle::OnCandidateResolutionAuthorized(
    core_mojom::BackupRestoreRecoveryResolutionAuthorizationResultPtr result) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!pending_candidate_resolution_) {
    return;
  }
  auto& pending = *pending_candidate_resolution_;
  CoreServiceManager* const manager =
      CurrentManager(profile_manager_, local_state_, pending.target,
                     pending.reservation, pending.records);
  if (!manager || !result ||
      result->status != core_mojom::BackupRestoreProtocolStatus::kSucceeded ||
      !result->authorization ||
      !ExactResolutionAuthority(*result->authorization, pending.target,
                                pending.records, pending.choice,
                                pending.intent_id, *manager) ||
      !TargetIsHiddenAndCoreless(profile_manager_,
                                 pending.target.target_profile_path)) {
    FinishCandidateResolution(base::unexpected(Error::kHistoryRefused));
    return;
  }
  pending.authorization = std::move(result->authorization);
  auto intent = BeginBackupRestoreResolutionIntent(
      local_state_, pending.target.reservation_id, *pending.authorization);
  // `base::expected` disables `operator bool` when the value type is itself
  // bool-convertible, which a mojo StructPtr is; ask for the value directly.
  if (!intent.has_value()) {
    FinishCandidateResolution(base::unexpected(Error::kRegistryRefused));
    return;
  }
  pending.intent = std::move(*intent);
  pending.persistence_witnesses = resolution::CaptureResolutionWitnesses(
      *local_state_, pending.target.target_profile_path, true);
  if (pending.persistence_witnesses.empty()) {
    FinishCandidateResolution(base::unexpected(Error::kPersistenceFailed));
    return;
  }
  local_state_->CommitPendingWrite(base::BindOnce(
      &BrowserProfilesRestoreLifecycle::OnCandidateResolutionIntentWriteDrained,
      weak_factory_.GetWeakPtr()));
}

void BrowserProfilesRestoreLifecycle::
    OnCandidateResolutionIntentWriteDrained() {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!pending_candidate_resolution_) {
    return;
  }
  auto witnesses =
      std::move(pending_candidate_resolution_->persistence_witnesses);
  resolution::VerifyResolutionPreferences(
      profile_manager_, std::move(witnesses),
      base::BindOnce(
          &BrowserProfilesRestoreLifecycle::OnCandidateResolutionIntentReadBack,
          weak_factory_.GetWeakPtr()));
}

void BrowserProfilesRestoreLifecycle::OnCandidateResolutionIntentReadBack(
    bool matches) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!pending_candidate_resolution_) {
    return;
  }
  auto& pending = *pending_candidate_resolution_;
  auto records = ReadBackupRestoreRecoveryJournal(
      local_state_, pending.target.reservation_id);
  if (!matches || !pending.intent || !records ||
      !JournalEndsWith(local_state_, pending.target.reservation_id,
                       *pending.intent) ||
      !pending.authorization ||
      !HistoryHasAuthorizedIntent(*pending.authorization, *records,
                                  pending.intent.get())) {
    FinishCandidateResolution(base::unexpected(Error::kPersistenceFailed));
    return;
  }
  pending.records = std::move(*records);
  DispatchCandidateResolutionOrRecordRefusal();
}

void BrowserProfilesRestoreLifecycle::
    DispatchCandidateResolutionOrRecordRefusal() {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!pending_candidate_resolution_) {
    return;
  }
  auto& pending = *pending_candidate_resolution_;
  CoreServiceManager* const manager =
      CurrentManager(profile_manager_, local_state_, pending.target,
                     pending.reservation, pending.records);
  if (!manager || !pending.authorization ||
      !LiveResolutionAuthority(*pending.authorization, pending.target,
                               pending.choice, pending.intent_id, *manager) ||
      !HistoryHasAuthorizedIntent(*pending.authorization, pending.records,
                                  pending.intent.get()) ||
      !TargetIsHiddenAndCoreless(profile_manager_,
                                 pending.target.target_profile_path)) {
    PersistCandidateResolutionOutcome(
        core_mojom::BackupRestoreResolutionOutcome::kDefinitelyNotCompleted);
    return;
  }

  pending.physical_action_dispatched = true;
  if (pending.choice ==
      core_mojom::BackupRestoreResolutionChoice::kAcceptCandidate) {
    pending.blocking_owner
        .AsyncCall(&CandidateResolutionBlockingOwner::FinalizeAccept)
        .Then(base::BindOnce(
            &BrowserProfilesRestoreLifecycle::OnCandidateAcceptStorageFinalized,
            weak_factory_.GetWeakPtr()));
    return;
  }
  const base::FilePath& target_path = pending.target.target_profile_path;
  if (!ScheduleProfileDirectoryForDeletion(target_path)) {
    PersistCandidateResolutionOutcome(
        core_mojom::BackupRestoreResolutionOutcome::kOutcomeUnknown);
    return;
  }
  if (!PersistProfileDirectoryDeletionMarker(target_path)) {
    CancelProfileDeletion(target_path);
    PersistCandidateResolutionOutcome(
        core_mojom::BackupRestoreResolutionOutcome::kOutcomeUnknown);
    return;
  }
  pending.persistence_witnesses =
      resolution::CaptureResolutionWitnesses(*local_state_, target_path, true);
  if (pending.persistence_witnesses.empty()) {
    CancelProfileDeletion(target_path);
    PersistCandidateResolutionOutcome(
        core_mojom::BackupRestoreResolutionOutcome::kOutcomeUnknown);
    return;
  }
  local_state_->CommitPendingWrite(base::BindOnce(
      &BrowserProfilesRestoreLifecycle::OnCandidateDiscardMarkerWriteDrained,
      weak_factory_.GetWeakPtr()));
}

void BrowserProfilesRestoreLifecycle::OnCandidateAcceptStorageFinalized(
    bool finalized) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!pending_candidate_resolution_) {
    return;
  }
  if (!finalized) {
    PersistCandidateResolutionOutcome(
        core_mojom::BackupRestoreResolutionOutcome::kDefinitelyNotCompleted);
    return;
  }
  auto& pending = *pending_candidate_resolution_;
  ProfileAttributesEntry* const entry =
      profile_manager_->GetProfileAttributesStorage()
          .GetProfileAttributesWithPath(pending.target.target_profile_path);
  Profile* const target =
      profile_manager_->GetProfileByPath(pending.target.target_profile_path);
  if (!entry || !entry->IsEphemeral() ||
      (target &&
       CoreServiceManagerFactory::HasExistingInstanceForProfile(target))) {
    PersistCandidateResolutionOutcome(
        core_mojom::BackupRestoreResolutionOutcome::kOutcomeUnknown);
    return;
  }
  // Chromium requires omission to be cleared before the durable ephemeral bit.
  // The registry quarantine remains in force throughout both notifications.
  entry->SetIsOmitted(false);
  entry->SetIsEphemeral(false);
  pending.persistence_witnesses = resolution::CaptureResolutionWitnesses(
      *local_state_, pending.target.target_profile_path, true);
  if (pending.persistence_witnesses.empty()) {
    PersistCandidateResolutionOutcome(
        core_mojom::BackupRestoreResolutionOutcome::kOutcomeUnknown);
    return;
  }
  local_state_->CommitPendingWrite(base::BindOnce(
      &BrowserProfilesRestoreLifecycle::OnCandidateAcceptMetadataWriteDrained,
      weak_factory_.GetWeakPtr()));
}

void BrowserProfilesRestoreLifecycle::OnCandidateAcceptMetadataWriteDrained() {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!pending_candidate_resolution_) {
    return;
  }
  auto witnesses =
      std::move(pending_candidate_resolution_->persistence_witnesses);
  resolution::VerifyResolutionPreferences(
      profile_manager_, std::move(witnesses),
      base::BindOnce(
          &BrowserProfilesRestoreLifecycle::OnCandidateAcceptMetadataReadBack,
          weak_factory_.GetWeakPtr()));
}

void BrowserProfilesRestoreLifecycle::OnCandidateAcceptMetadataReadBack(
    bool matches) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!pending_candidate_resolution_) {
    return;
  }
  ProfileAttributesEntry* const entry =
      profile_manager_->GetProfileAttributesStorage()
          .GetProfileAttributesWithPath(
              pending_candidate_resolution_->target.target_profile_path);
  PersistCandidateResolutionOutcome(
      matches && entry && !entry->IsOmitted() && !entry->IsEphemeral()
          ? core_mojom::BackupRestoreResolutionOutcome::kCompleted
          : core_mojom::BackupRestoreResolutionOutcome::kOutcomeUnknown);
}

}  // namespace taffy

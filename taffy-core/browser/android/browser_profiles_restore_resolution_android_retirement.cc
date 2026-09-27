// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/strings/strcat.h"
#include "chrome/browser/profiles/nuke_profile_directory_utils.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_attributes_entry.h"
#include "chrome/browser/profiles/profile_attributes_storage.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/common/pref_names.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/browser_thread.h"
#include "taffy/browser/android/browser_profiles_restore_resolution_android_internal.h"
#include "taffy/browser/application_preferences.h"
#include "taffy/browser/backup_restore_reservation_retirement.h"
#include "taffy/browser/core_service_manager_factory.h"

namespace taffy {
namespace {

namespace mojom = core_service::mojom;
namespace resolution = restore_resolution_internal;
using Error = BackupRestoreCandidateResolutionError;
using Witness = BackupRestorePreferenceWriteWitness;

bool IsAccept(mojom::BackupRestoreResolutionChoice choice) {
  return choice == mojom::BackupRestoreResolutionChoice::kAcceptCandidate;
}

bool IsRegularTarget(ProfileManager* manager,
                     const base::FilePath& target_path) {
  ProfileAttributesEntry* const entry =
      manager->GetProfileAttributesStorage().GetProfileAttributesWithPath(
          target_path);
  Profile* const profile = manager->GetProfileByPath(target_path);
  return entry && !entry->IsOmitted() && !entry->IsEphemeral() &&
         (!profile ||
          !CoreServiceManagerFactory::HasExistingInstanceForProfile(profile));
}

bool IsHiddenTarget(ProfileManager* manager,
                    const base::FilePath& target_path) {
  ProfileAttributesEntry* const entry =
      manager->GetProfileAttributesStorage().GetProfileAttributesWithPath(
          target_path);
  Profile* const profile = manager->GetProfileByPath(target_path);
  return entry && entry->IsEphemeral() &&
         (!profile ||
          !CoreServiceManagerFactory::HasExistingInstanceForProfile(profile));
}

std::vector<Witness> CaptureRetirementWitnesses(
    const PrefService& local_state,
    const base::FilePath& target_path) {
  const std::string target_key = target_path.BaseName().AsUTF8Unsafe();
  const base::Value* target_attributes =
      local_state.GetDict(prefs::kProfileAttributes).Find(target_key);
  Witness attributes{.dotted_path = base::StrCat(
                         {prefs::kProfileAttributes, ".", target_key})};
  if (target_attributes) {
    attributes.expected = target_attributes->Clone();
  } else {
    attributes.must_be_absent = true;
  }
  std::vector<Witness> witnesses;
  witnesses.push_back(
      {.dotted_path =
           application_preferences::kBackupRestoreProfileReservations,
       .must_be_absent = true});
  witnesses.push_back(std::move(attributes));
  witnesses.push_back(
      {.dotted_path = prefs::kProfilesOrder,
       .expected =
           base::Value(local_state.GetList(prefs::kProfilesOrder).Clone())});
  witnesses.push_back(
      {.dotted_path = prefs::kProfilesDeleted,
       .expected =
           base::Value(local_state.GetList(prefs::kProfilesDeleted).Clone())});
  return witnesses;
}

}  // namespace

void BrowserProfilesRestoreLifecycle::BeginCandidateResolutionRetirement() {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!pending_candidate_resolution_ ||
      !pending_candidate_resolution_->classification) {
    return;
  }
  auto& pending = *pending_candidate_resolution_;
  pending.blocking_owner
      .AsyncCall(&CandidateResolutionBlockingOwner::VerifyTerminalPhysical)
      .WithArgs(pending.target.target_profile_path,
                mojom::BackupRestoreTarget::New(
                    mojom::BackupRestoreTargetKind::kNewRegularProfile,
                    pending.target.target_profile_id),
                pending.choice)
      .Then(base::BindOnce(&BrowserProfilesRestoreLifecycle::
                               OnCandidateResolutionPhysicalRetirementChecked,
                           weak_factory_.GetWeakPtr()));
}

void BrowserProfilesRestoreLifecycle::
    OnCandidateResolutionPhysicalRetirementChecked(bool valid) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!pending_candidate_resolution_ ||
      !pending_candidate_resolution_->classification) {
    return;
  }
  auto& pending = *pending_candidate_resolution_;
  if (pending.classification->kind ==
      mojom::BackupRestoreRecoveryClassificationKind::kReconcileRequired) {
    mojom::BackupRestoreResolutionOutcome outcome =
        mojom::BackupRestoreResolutionOutcome::kOutcomeUnknown;
    if (IsAccept(pending.choice)) {
      if (valid && IsRegularTarget(profile_manager_,
                                   pending.target.target_profile_path)) {
        outcome = mojom::BackupRestoreResolutionOutcome::kCompleted;
      } else if (valid && IsHiddenTarget(profile_manager_,
                                         pending.target.target_profile_path)) {
        outcome =
            mojom::BackupRestoreResolutionOutcome::kDefinitelyNotCompleted;
      }
    } else if (valid && CompleteProfileDirectoryDeletionMarker(
                            pending.target.target_profile_path)) {
      outcome = mojom::BackupRestoreResolutionOutcome::kCompleted;
    }
    PersistCandidateResolutionOutcome(outcome);
    return;
  }
  const bool metadata_valid =
      IsAccept(pending.choice)
          ? IsRegularTarget(profile_manager_,
                            pending.target.target_profile_path)
          : !profile_manager_->GetProfileAttributesStorage()
                    .GetProfileAttributesWithPath(
                        pending.target.target_profile_path) &&
                !profile_manager_->GetProfileByPath(
                    pending.target.target_profile_path) &&
                CompleteProfileDirectoryDeletionMarker(
                    pending.target.target_profile_path);
  if (!valid || !metadata_valid) {
    FinishCandidateResolution(base::unexpected(Error::kProfileStateRefused));
    return;
  }
  pending.blocking_owner.AsyncCall(&CandidateResolutionBlockingOwner::Close)
      .Then(base::BindOnce(&BrowserProfilesRestoreLifecycle::
                               OnCandidateResolutionPhysicalCustodyReleased,
                           weak_factory_.GetWeakPtr()));
}

void BrowserProfilesRestoreLifecycle::
    OnCandidateResolutionPhysicalCustodyReleased(bool released) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!pending_candidate_resolution_) {
    return;
  }
  auto& pending = *pending_candidate_resolution_;
  if (!released) {
    FinishCandidateResolution(base::unexpected(Error::kStorageUnavailable));
    return;
  }
  pending.blocking_owner_closed = true;
  if (pending.retirement) {
    if (!pending.retirement->ReleaseAfterVerifiedAbsence()) {
      FinishCandidateResolution(base::unexpected(Error::kPersistenceFailed));
      return;
    }
    FinishCandidateResolution(BackupRestoreCandidateResolution{
        .classification = pending.classification.Clone(),
        .physical_action_dispatched = pending.physical_action_dispatched,
        .reservation_retired = true,
    });
    return;
  }
  if (!resolution::ExactCurrentState(profile_manager_, local_state_,
                                     pending.target, pending.reservation,
                                     pending.records)) {
    FinishCandidateResolution(base::unexpected(Error::kPersistenceFailed));
    return;
  }
  auto retirement = BackupRestoreReservationRetirement::Begin(
      local_state_, pending.reservation, pending.records,
      *pending.classification);
  if (!retirement.has_value()) {
    FinishCandidateResolution(base::unexpected(Error::kRegistryRefused));
    return;
  }
  pending.retirement = std::move(*retirement);
  pending.persistence_witnesses = CaptureRetirementWitnesses(
      *local_state_, pending.target.target_profile_path);
  local_state_->CommitPendingWrite(
      base::BindOnce(&BrowserProfilesRestoreLifecycle::
                         OnCandidateResolutionRetirementWriteDrained,
                     weak_factory_.GetWeakPtr()));
}

void BrowserProfilesRestoreLifecycle::
    OnCandidateResolutionRetirementWriteDrained() {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!pending_candidate_resolution_) {
    return;
  }
  auto witnesses =
      std::move(pending_candidate_resolution_->persistence_witnesses);
  resolution::VerifyResolutionPreferences(
      profile_manager_, std::move(witnesses),
      base::BindOnce(&BrowserProfilesRestoreLifecycle::
                         OnCandidateResolutionRetirementReadBack,
                     weak_factory_.GetWeakPtr()));
}

void BrowserProfilesRestoreLifecycle::OnCandidateResolutionRetirementReadBack(
    bool matches) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!pending_candidate_resolution_ ||
      !pending_candidate_resolution_->retirement) {
    return;
  }
  if (!matches) {
    FinishCandidateResolution(base::unexpected(Error::kPersistenceFailed));
    return;
  }
  BeginCandidateResolutionRetirement();
}

void BrowserProfilesRestoreLifecycle::FinishCandidateResolution(
    CandidateResolutionResult result) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!pending_candidate_resolution_ ||
      pending_candidate_resolution_->finish_result) {
    return;
  }
  if (!pending_candidate_resolution_->blocking_owner_closed) {
    // A returned nonterminal classification is immediately eligible for a
    // new explicit decision. Release any finalizer/deletion/path lease before
    // publishing it, so that decision cannot observe a transient false Busy.
    pending_candidate_resolution_->finish_result.emplace(std::move(result));
    pending_candidate_resolution_->blocking_owner
        .AsyncCall(&CandidateResolutionBlockingOwner::Close)
        .Then(base::BindOnce(
            &BrowserProfilesRestoreLifecycle::OnCandidateResolutionOwnerClosed,
            weak_factory_.GetWeakPtr()));
    return;
  }
  CandidateResolutionCallback callback =
      std::move(pending_candidate_resolution_->callback);
  pending_candidate_resolution_.reset();
  std::move(callback).Run(std::move(result));
}

void BrowserProfilesRestoreLifecycle::OnCandidateResolutionOwnerClosed(
    bool closed) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!pending_candidate_resolution_ ||
      !pending_candidate_resolution_->finish_result) {
    return;
  }
  CandidateResolutionResult result =
      std::move(*pending_candidate_resolution_->finish_result);
  pending_candidate_resolution_->finish_result.reset();
  pending_candidate_resolution_->blocking_owner_closed = true;
  if (!closed) {
    result = base::unexpected(Error::kStorageUnavailable);
  }
  FinishCandidateResolution(std::move(result));
}

}  // namespace taffy

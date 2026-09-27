// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <utility>

#include "base/functional/bind.h"
#include "chrome/browser/profiles/nuke_profile_directory_utils.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/browser_thread.h"
#include "taffy/browser/android/browser_profiles_restore_resolution_android_internal.h"
#include "taffy/browser/backup_restore_recovery_journal.h"

namespace taffy {
namespace {

namespace mojom = core_service::mojom;
namespace resolution = restore_resolution_internal;

bool ExactIntentStillEndsJournal(
    const PrefService* local_state,
    const std::string& reservation_id,
    const mojom::BackupRestoreRecoveryRecord* intent) {
  auto records = ReadBackupRestoreRecoveryJournal(local_state, reservation_id);
  return intent && records && !records->empty() &&
         records->size() == intent->sequence && records->back() &&
         records->back()->Equals(*intent);
}

}  // namespace

void BrowserProfilesRestoreLifecycle::OnCandidateDiscardMarkerWriteDrained() {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!pending_candidate_resolution_) {
    return;
  }
  auto witnesses =
      std::move(pending_candidate_resolution_->persistence_witnesses);
  resolution::VerifyResolutionPreferences(
      profile_manager_, std::move(witnesses),
      base::BindOnce(
          &BrowserProfilesRestoreLifecycle::OnCandidateDiscardMarkerReadBack,
          weak_factory_.GetWeakPtr()));
}

void BrowserProfilesRestoreLifecycle::OnCandidateDiscardMarkerReadBack(
    bool matches) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!pending_candidate_resolution_) {
    return;
  }
  auto& pending = *pending_candidate_resolution_;
  const base::FilePath target_path = pending.target.target_profile_path;
  if (!matches ||
      !ExactIntentStillEndsJournal(local_state_, pending.target.reservation_id,
                                   pending.intent.get()) ||
      !ArmProfileDirectoryForDeletion(target_path)) {
    CancelProfileDeletion(target_path);
    PersistCandidateResolutionOutcome(
        mojom::BackupRestoreResolutionOutcome::kOutcomeUnknown);
    return;
  }
  if (!profile_manager_->DeleteMarkedEphemeralProfileOnAndroid(
          target_path,
          base::BindOnce(
              &BrowserProfilesRestoreLifecycle::OnCandidateDiscardDeleted,
              weak_factory_.GetWeakPtr()))) {
    PersistCandidateResolutionOutcome(
        mojom::BackupRestoreResolutionOutcome::kOutcomeUnknown);
  }
}

void BrowserProfilesRestoreLifecycle::OnCandidateDiscardDeleted(bool deleted) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!pending_candidate_resolution_) {
    return;
  }
  pending_candidate_resolution_->blocking_owner
      .AsyncCall(
          &CandidateResolutionBlockingOwner::VerifyDiscardAfterChromiumDeletion)
      .WithArgs(deleted)
      .Then(base::BindOnce(
          &BrowserProfilesRestoreLifecycle::OnCandidateDiscardVerified,
          weak_factory_.GetWeakPtr()));
}

void BrowserProfilesRestoreLifecycle::OnCandidateDiscardVerified(
    bool verified) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!pending_candidate_resolution_) {
    return;
  }
  PersistCandidateResolutionOutcome(
      verified ? mojom::BackupRestoreResolutionOutcome::kCompleted
               : mojom::BackupRestoreResolutionOutcome::kOutcomeUnknown);
}

}  // namespace taffy

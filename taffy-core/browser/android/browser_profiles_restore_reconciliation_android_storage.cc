// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <utility>

#include "taffy/browser/android/browser_profiles_restore_lifecycle_internal.h"
#include "taffy/components/storage/browser/dormant_backup_restore_target_reconciler.h"

namespace taffy {

namespace mojom = core_service::mojom;

BrowserProfilesRestoreLifecycle::CommitReconciliationBlockingOwner::
    CommitReconciliationBlockingOwner() = default;

BrowserProfilesRestoreLifecycle::CommitReconciliationBlockingOwner::
    ~CommitReconciliationBlockingOwner() = default;

BrowserProfilesRestoreLifecycle::CommitReconciliationStorageResult
BrowserProfilesRestoreLifecycle::CommitReconciliationBlockingOwner::Reconcile(
    base::FilePath target_profile_path,
    mojom::BackupRestoreTargetPtr exact_target,
    mojom::BackupRestoreCandidateWitnessPtr witness) {
  auto reconciler =
      storage::backup::DormantBackupRestoreTargetReconciler::OpenForRecovery(
          target_profile_path, std::move(exact_target));
  if (!reconciler.has_value()) {
    return base::unexpected(reconciler.error());
  }
  auto state = (*reconciler)->Reconcile(std::move(witness));
  if (!state) {
    return base::unexpected(state.error());
  }
  switch (*state) {
    case storage::backup::DormantBackupRestoreReconcileState::kPristine:
      return mojom::BackupRestoreCommitOutcome::kDefinitelyNotCommitted;
    case storage::backup::DormantBackupRestoreReconcileState::kCommitted:
      return mojom::BackupRestoreCommitOutcome::kCommitted;
    case storage::backup::DormantBackupRestoreReconcileState::kOutcomeUnknown:
      return mojom::BackupRestoreCommitOutcome::kOutcomeUnknown;
  }
  return base::unexpected(
      storage::backup::DormantBackupRestoreReconcileError::kStorageUnavailable);
}

}  // namespace taffy

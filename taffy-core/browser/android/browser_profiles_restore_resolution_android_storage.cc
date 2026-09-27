// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <memory>
#include <utility>

#include "chrome/browser/profiles/nuke_profile_directory_utils.h"
#include "taffy/browser/android/browser_profiles_restore_resolution_android_internal.h"
#include "taffy/components/storage/browser/dormant_backup_restore_target_lease.h"
#include "taffy/components/storage/browser/dormant_backup_restore_target_resolution.h"

namespace taffy {
namespace {

namespace mojom = core_service::mojom;
using Error = BackupRestoreCandidateResolutionError;
using ResolutionError = storage::backup::DormantBackupRestoreResolutionError;
using DeletionCustody =
    storage::backup::DormantBackupRestoreTargetDeletionCustody;
using Finalizer = storage::backup::DormantBackupRestoreTargetFinalizer;
using Lease = storage::backup::DormantBackupRestoreTargetLease;

Error MapResolutionError(ResolutionError error) {
  switch (error) {
    case ResolutionError::kTargetBusy:
      return Error::kBusy;
    case ResolutionError::kInvalidTarget:
    case ResolutionError::kTargetChanged:
    case ResolutionError::kSchemaMismatch:
    case ResolutionError::kInvalidWitness:
    case ResolutionError::kCandidateMismatch:
    case ResolutionError::kStageMismatch:
      return Error::kProfileStateRefused;
    case ResolutionError::kStorageUnavailable:
    case ResolutionError::kCleanupFailed:
      return Error::kStorageUnavailable;
  }
  return Error::kStorageUnavailable;
}

base::FilePath DatabasePath(const base::FilePath& profile_path) {
  return profile_path.AppendASCII("TaffyCore").AppendASCII("core.sqlite3");
}

}  // namespace

class BrowserProfilesRestoreLifecycle::CandidateResolutionBlockingOwner::
    StorageOwner {
 public:
  std::unique_ptr<Finalizer> finalizer;
  std::unique_ptr<DeletionCustody> deletion;
  std::unique_ptr<Lease> terminal_deletion_lease;
};

BrowserProfilesRestoreLifecycle::CandidateResolutionBlockingOwner::
    CandidateResolutionBlockingOwner()
    : storage_owner_(std::make_unique<StorageOwner>()) {}

BrowserProfilesRestoreLifecycle::CandidateResolutionBlockingOwner::
    ~CandidateResolutionBlockingOwner() = default;

base::expected<void, Error>
BrowserProfilesRestoreLifecycle::CandidateResolutionBlockingOwner::Prepare(
    base::FilePath target_profile_path,
    mojom::BackupRestoreTargetPtr exact_target,
    mojom::BackupRestoreCandidateWitnessPtr exact_witness,
    mojom::BackupRestoreResolutionChoice choice) {
  if (storage_owner_->finalizer || storage_owner_->deletion ||
      storage_owner_->terminal_deletion_lease) {
    return base::unexpected(Error::kBusy);
  }
  switch (choice) {
    case mojom::BackupRestoreResolutionChoice::kAcceptCandidate: {
      auto opened =
          Finalizer::OpenForAccept(target_profile_path, std::move(exact_target),
                                   std::move(exact_witness));
      if (!opened.has_value()) {
        return base::unexpected(MapResolutionError(opened.error()));
      }
      storage_owner_->finalizer = std::move(*opened);
      return base::ok();
    }
    case mojom::BackupRestoreResolutionChoice::kDiscardCandidate: {
      auto opened = DeletionCustody::OpenForDiscard(target_profile_path,
                                                    std::move(exact_target));
      if (!opened.has_value()) {
        return base::unexpected(MapResolutionError(opened.error()));
      }
      if (!(*opened)->PrepareForProfileDeletion()) {
        return base::unexpected(Error::kStorageUnavailable);
      }
      storage_owner_->deletion = std::move(*opened);
      return base::ok();
    }
  }
  return base::unexpected(Error::kInvalidArgument);
}

bool BrowserProfilesRestoreLifecycle::CandidateResolutionBlockingOwner::
    FinalizeAccept() {
  return storage_owner_->finalizer &&
         storage_owner_->finalizer->FinalizeForPublication().has_value();
}

bool BrowserProfilesRestoreLifecycle::CandidateResolutionBlockingOwner::
    VerifyDiscardAfterChromiumDeletion(bool chromium_deleted) {
  return chromium_deleted && storage_owner_->deletion &&
         storage_owner_->deletion->VerifyProfileDeleted();
}

bool BrowserProfilesRestoreLifecycle::CandidateResolutionBlockingOwner::
    VerifyTerminalPhysical(base::FilePath target_profile_path,
                           mojom::BackupRestoreTargetPtr exact_target,
                           mojom::BackupRestoreResolutionChoice choice) {
  if (storage_owner_->finalizer || storage_owner_->deletion ||
      storage_owner_->terminal_deletion_lease) {
    if (choice == mojom::BackupRestoreResolutionChoice::kAcceptCandidate &&
        storage_owner_->finalizer) {
      return storage_owner_->finalizer->VerifyPublishedIdentity();
    }
    if (choice == mojom::BackupRestoreResolutionChoice::kDiscardCandidate &&
        storage_owner_->deletion) {
      return storage_owner_->deletion->VerifyProfileDeleted() &&
             VerifyProfileAndCacheDirectoryDeletion(target_profile_path);
    }
    return false;
  }
  if (choice == mojom::BackupRestoreResolutionChoice::kAcceptCandidate) {
    return storage::backup::VerifyPublishedDormantBackupRestoreTarget(
               target_profile_path, std::move(exact_target))
        .has_value();
  }
  storage_owner_->terminal_deletion_lease =
      Lease::TryAcquireForAbsentTarget(DatabasePath(target_profile_path));
  return storage_owner_->terminal_deletion_lease &&
         VerifyProfileAndCacheDirectoryDeletion(target_profile_path);
}

bool BrowserProfilesRestoreLifecycle::CandidateResolutionBlockingOwner::
    Close() {
  if (!storage_owner_) {
    return true;
  }
  storage_owner_.reset();
  storage_owner_ = std::make_unique<StorageOwner>();
  return true;
}

}  // namespace taffy

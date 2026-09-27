// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <memory>
#include <utility>

#include "base/files/file_util.h"
#include "chrome/browser/profiles/nuke_profile_directory_utils.h"
#include "taffy/browser/android/browser_profiles_restore_precommit_cleanup_internal.h"
#include "taffy/components/storage/browser/dormant_backup_restore_target_lease.h"
#include "taffy/components/storage/browser/dormant_backup_restore_target_resolution.h"
#include "taffy/components/storage/browser/dormant_backup_restore_target_resolution_internal.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"

namespace taffy {
namespace {

namespace storage_internal = storage::backup::restore_resolution_internal;
using CleanupError = BackupRestorePrecommitCleanupError;
using DeletionCustody =
    storage::backup::DormantBackupRestoreTargetDeletionCustody;
using Lease = storage::backup::DormantBackupRestoreTargetLease;
using ResolutionError = storage::backup::DormantBackupRestoreResolutionError;

CleanupError MapResolutionError(ResolutionError error) {
  switch (error) {
    case ResolutionError::kTargetBusy:
      return CleanupError::kBusy;
    case ResolutionError::kInvalidTarget:
    case ResolutionError::kTargetChanged:
    case ResolutionError::kSchemaMismatch:
    case ResolutionError::kInvalidWitness:
    case ResolutionError::kCandidateMismatch:
    case ResolutionError::kStageMismatch:
      return CleanupError::kProfileStateRefused;
    case ResolutionError::kStorageUnavailable:
    case ResolutionError::kCleanupFailed:
      return CleanupError::kStorageUnavailable;
  }
  return CleanupError::kStorageUnavailable;
}

base::FilePath DatabasePath(const base::FilePath& profile_path) {
  return profile_path.AppendASCII("TaffyCore").AppendASCII("core.sqlite3");
}

}  // namespace

class BrowserProfilesRestoreLifecycle::PrecommitCleanupBlockingOwner::
    StorageOwner {
 public:
  base::FilePath profile_path;
  std::unique_ptr<DeletionCustody> initialized_target;
  std::unique_ptr<Lease> pristine_target_lease;
  base::File held_profile_parent;
  base::File held_profile;
  bool pristine_prepared = false;
};

BrowserProfilesRestoreLifecycle::PrecommitCleanupBlockingOwner::
    PrecommitCleanupBlockingOwner()
    : storage_owner_(std::make_unique<StorageOwner>()) {}

BrowserProfilesRestoreLifecycle::PrecommitCleanupBlockingOwner::
    ~PrecommitCleanupBlockingOwner() = default;

base::expected<void, CleanupError>
BrowserProfilesRestoreLifecycle::PrecommitCleanupBlockingOwner::Prepare(
    base::FilePath target_profile_path,
    std::optional<std::string> target_profile_id) {
  if (!storage_owner_ || storage_owner_->initialized_target ||
      storage_owner_->pristine_target_lease || target_profile_path.empty()) {
    return base::unexpected(CleanupError::kBusy);
  }
  const base::FilePath database_path = DatabasePath(target_profile_path);
  const base::FilePath core_path = database_path.DirName();
  if (base::PathExists(database_path) || base::IsLink(database_path) ||
      base::PathExists(core_path) || base::IsLink(core_path)) {
    if (!target_profile_id) {
      return base::unexpected(CleanupError::kProfileStateRefused);
    }
    auto custody = DeletionCustody::OpenForDiscard(
        target_profile_path,
        core_service::mojom::BackupRestoreTarget::New(
            core_service::mojom::BackupRestoreTargetKind::kNewRegularProfile,
            *target_profile_id));
    if (!custody.has_value()) {
      return base::unexpected(MapResolutionError(custody.error()));
    }
    if (!(*custody)->PrepareForProfileDeletion()) {
      return base::unexpected(CleanupError::kStorageUnavailable);
    }
    storage_owner_->profile_path = std::move(target_profile_path);
    storage_owner_->initialized_target = std::move(*custody);
    return base::ok();
  }
  if (!storage_internal::IsRegularDirectoryWithoutLinks(target_profile_path)) {
    return base::unexpected(CleanupError::kProfileStateRefused);
  }
  auto lease = Lease::TryAcquireForAbsentTarget(database_path);
  base::File parent = storage_internal::OpenPath(target_profile_path.DirName());
  base::File profile = storage_internal::OpenPath(target_profile_path);
  if (!lease ||
      !storage_internal::SameOpenedPath(parent, target_profile_path.DirName(),
                                        true) ||
      !storage_internal::SameOpenedPath(profile, target_profile_path, true) ||
      !storage_internal::PathIsAbsent(core_path)) {
    return base::unexpected(lease ? CleanupError::kProfileStateRefused
                                  : CleanupError::kBusy);
  }
  storage_owner_->profile_path = std::move(target_profile_path);
  storage_owner_->pristine_target_lease = std::move(lease);
  storage_owner_->held_profile_parent = std::move(parent);
  storage_owner_->held_profile = std::move(profile);
  storage_owner_->pristine_prepared = true;
  return base::ok();
}

bool BrowserProfilesRestoreLifecycle::PrecommitCleanupBlockingOwner::
    VerifyAfterChromiumDeletion(bool chromium_deleted) {
  if (!storage_owner_ || !chromium_deleted ||
      storage_owner_->profile_path.empty()) {
    return false;
  }
  bool exact_profile_deleted = false;
  if (storage_owner_->initialized_target) {
    exact_profile_deleted =
        storage_owner_->initialized_target->VerifyProfileDeleted();
  } else if (storage_owner_->pristine_prepared &&
             storage_owner_->pristine_target_lease) {
    exact_profile_deleted =
        storage_internal::PathIsAbsent(storage_owner_->profile_path) &&
        storage_internal::OpenedPathWasUnlinked(
            storage_owner_->held_profile, storage_owner_->profile_path, true) &&
        storage_internal::SyncOpenedDirectory(
            storage_owner_->held_profile_parent);
  }
  return exact_profile_deleted &&
         VerifyProfileAndCacheDirectoryDeletion(storage_owner_->profile_path);
}

bool BrowserProfilesRestoreLifecycle::PrecommitCleanupBlockingOwner::Close() {
  if (!storage_owner_) {
    return false;
  }
  storage_owner_.reset();
  storage_owner_ = std::make_unique<StorageOwner>();
  return true;
}

}  // namespace taffy

// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <utility>

#include "taffy/browser/android/browser_profiles_restore_precommit_cleanup.h"
#include "taffy/browser/profile_backup_restore_target.h"

namespace taffy {

PrecommitBackupRestoreCleanupHandle::PrecommitBackupRestoreCleanupHandle(
    base::Token lifecycle_instance,
    std::string reservation_id,
    base::FilePath source_profile_path,
    base::FilePath target_profile_path,
    std::string target_profile_id)
    : lifecycle_instance_(lifecycle_instance),
      reservation_id_(std::move(reservation_id)),
      source_profile_path_(std::move(source_profile_path)),
      target_profile_path_(std::move(target_profile_path)),
      target_profile_id_(std::move(target_profile_id)) {}

PrecommitBackupRestoreCleanupHandle::PrecommitBackupRestoreCleanupHandle(
    PrecommitBackupRestoreCleanupHandle&& other) noexcept
    : lifecycle_instance_(
          std::exchange(other.lifecycle_instance_, base::Token())),
      reservation_id_(std::exchange(other.reservation_id_, std::string())),
      source_profile_path_(
          std::exchange(other.source_profile_path_, base::FilePath())),
      target_profile_path_(
          std::exchange(other.target_profile_path_, base::FilePath())),
      target_profile_id_(
          std::exchange(other.target_profile_id_, std::string())) {}

PrecommitBackupRestoreCleanupHandle&
PrecommitBackupRestoreCleanupHandle::operator=(
    PrecommitBackupRestoreCleanupHandle&& other) noexcept {
  if (this != &other) {
    lifecycle_instance_ =
        std::exchange(other.lifecycle_instance_, base::Token());
    reservation_id_ = std::exchange(other.reservation_id_, std::string());
    source_profile_path_ =
        std::exchange(other.source_profile_path_, base::FilePath());
    target_profile_path_ =
        std::exchange(other.target_profile_path_, base::FilePath());
    target_profile_id_ = std::exchange(other.target_profile_id_, std::string());
  }
  return *this;
}

PrecommitBackupRestoreCleanupHandle::~PrecommitBackupRestoreCleanupHandle() =
    default;

InitializedDormantBackupRestoreTarget::InitializedDormantBackupRestoreTarget(
    std::unique_ptr<ProfileBackupRestoreTarget> target,
    PrecommitBackupRestoreCleanupHandle cleanup)
    : target(std::move(target)), cleanup(std::move(cleanup)) {}

InitializedDormantBackupRestoreTarget::InitializedDormantBackupRestoreTarget(
    InitializedDormantBackupRestoreTarget&& other) noexcept = default;

InitializedDormantBackupRestoreTarget&
InitializedDormantBackupRestoreTarget::operator=(
    InitializedDormantBackupRestoreTarget&& other) noexcept = default;

InitializedDormantBackupRestoreTarget::
    ~InitializedDormantBackupRestoreTarget() = default;

}  // namespace taffy

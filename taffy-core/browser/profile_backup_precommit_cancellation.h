// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_PROFILE_BACKUP_PRECOMMIT_CANCELLATION_H_
#define TAFFY_BROWSER_PROFILE_BACKUP_PRECOMMIT_CANCELLATION_H_

#include <string>

#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"

namespace taffy {

class BackupRestoreReservationRetirement;
class BrowserProfilesRestoreLifecycle;
class ProfileBackupCoordinator;
class ProfileBackupCoordinatorTestPeer;

// One-use evidence that the coordinator closed one exact target before commit
// authority was ever requested. When the source Core had minted a plan, the
// receipt also retains that exact cancelled binding; a null binding proves the
// failure completed before a plan existed. This is physical cleanup evidence,
// never portable authority. Only the coordinator can mint it and moving
// consumes the source.
class ProfileBackupPrecommitCancellationReceipt {
 public:
  ProfileBackupPrecommitCancellationReceipt(
      ProfileBackupPrecommitCancellationReceipt&& other) noexcept;
  ProfileBackupPrecommitCancellationReceipt& operator=(
      ProfileBackupPrecommitCancellationReceipt&& other) noexcept;
  ProfileBackupPrecommitCancellationReceipt(
      const ProfileBackupPrecommitCancellationReceipt&) = delete;
  ProfileBackupPrecommitCancellationReceipt& operator=(
      const ProfileBackupPrecommitCancellationReceipt&) = delete;
  ~ProfileBackupPrecommitCancellationReceipt();

 private:
  friend class BackupRestoreReservationRetirement;
  friend class BrowserProfilesRestoreLifecycle;
  friend class ProfileBackupCoordinator;
  friend class ProfileBackupCoordinatorTestPeer;

  ProfileBackupPrecommitCancellationReceipt(
      std::string operation_id,
      std::string source_profile_id,
      std::string target_profile_id,
      core_service::mojom::BackupRestoreBindingPtr binding);
  void Invalidate();

  bool valid_ = false;
  std::string operation_id_;
  std::string source_profile_id_;
  std::string target_profile_id_;
  // Null means cancellation completed before the source Core minted a plan.
  // A non-null binding is the exact plan that was cancelled.
  core_service::mojom::BackupRestoreBindingPtr binding_;
};

}  // namespace taffy

#endif  // TAFFY_BROWSER_PROFILE_BACKUP_PRECOMMIT_CANCELLATION_H_

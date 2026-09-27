// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_COMPONENTS_STORAGE_BROWSER_DORMANT_BACKUP_RESTORE_TARGET_LEASE_H_
#define TAFFY_COMPONENTS_STORAGE_BROWSER_DORMANT_BACKUP_RESTORE_TARGET_LEASE_H_

#include <memory>

#include "base/files/file_path.h"

namespace taffy::storage::backup {

// Process-local exclusive physical custody for one canonical dormant target
// database path. Durable reservation authority remains browser-owned; this
// lease only prevents two live blocking-sequence storage owners from observing
// or mutating the same target concurrently.
class DormantBackupRestoreTargetLease {
 public:
  static std::unique_ptr<DormantBackupRestoreTargetLease> TryAcquire(
      const base::FilePath& canonical_database_path);

  // Acquires the same key after an authorized target deletion, when the
  // profile directory itself no longer exists. The existing user-data parent
  // is canonicalized and every missing child component is reconstructed
  // exactly; this grants no deletion authority.
  static std::unique_ptr<DormantBackupRestoreTargetLease>
  TryAcquireForAbsentTarget(const base::FilePath& canonical_database_path);

  DormantBackupRestoreTargetLease(const DormantBackupRestoreTargetLease&) =
      delete;
  DormantBackupRestoreTargetLease& operator=(
      const DormantBackupRestoreTargetLease&) = delete;
  ~DormantBackupRestoreTargetLease();

 private:
  // Both factories canonicalize their own path and then mint through here.
  // It is a member because the constructor is private: minting a lease is
  // only legitimate while the registry lock proves the key was unheld.
  static std::unique_ptr<DormantBackupRestoreTargetLease> Acquire(
      const base::FilePath& database_path);

  explicit DormantBackupRestoreTargetLease(base::FilePath database_path);

  const base::FilePath database_path_;
};

}  // namespace taffy::storage::backup

#endif  // TAFFY_COMPONENTS_STORAGE_BROWSER_DORMANT_BACKUP_RESTORE_TARGET_LEASE_H_

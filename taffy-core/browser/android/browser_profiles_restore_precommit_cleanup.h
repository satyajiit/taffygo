// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_ANDROID_BROWSER_PROFILES_RESTORE_PRECOMMIT_CLEANUP_H_
#define TAFFY_BROWSER_ANDROID_BROWSER_PROFILES_RESTORE_PRECOMMIT_CLEANUP_H_

#include <cstdint>
#include <memory>
#include <string>

#include "base/files/file_path.h"
#include "base/token.h"

namespace taffy {

class BrowserProfilesRestoreLifecycle;
class ProfileBackupRestoreTarget;

// A process-local, one-use capability for removing only the fresh hidden
// profile created by the same lifecycle incarnation. It is minted together
// with the initialized dormant target so it cannot be copied or recreated
// from a persisted reservation id. It is physical custody, not portable
// cancellation or deletion authority.
class PrecommitBackupRestoreCleanupHandle {
 public:
  PrecommitBackupRestoreCleanupHandle(
      PrecommitBackupRestoreCleanupHandle&& other) noexcept;
  PrecommitBackupRestoreCleanupHandle& operator=(
      PrecommitBackupRestoreCleanupHandle&& other) noexcept;
  PrecommitBackupRestoreCleanupHandle(
      const PrecommitBackupRestoreCleanupHandle&) = delete;
  PrecommitBackupRestoreCleanupHandle& operator=(
      const PrecommitBackupRestoreCleanupHandle&) = delete;
  ~PrecommitBackupRestoreCleanupHandle();

 private:
  friend class BrowserProfilesRestoreLifecycle;

  PrecommitBackupRestoreCleanupHandle(base::Token lifecycle_instance,
                                      std::string reservation_id,
                                      base::FilePath source_profile_path,
                                      base::FilePath target_profile_path,
                                      std::string target_profile_id);

  base::Token lifecycle_instance_;
  std::string reservation_id_;
  base::FilePath source_profile_path_;
  base::FilePath target_profile_path_;
  std::string target_profile_id_;
};

// Initialization transfers the mutable target and exactly one correlated
// precommit cleanup capability. The caller gives the target to the portable
// coordinator but retains `cleanup` until cancellation has fully drained it.
struct InitializedDormantBackupRestoreTarget {
  InitializedDormantBackupRestoreTarget(
      std::unique_ptr<ProfileBackupRestoreTarget> target,
      PrecommitBackupRestoreCleanupHandle cleanup);
  InitializedDormantBackupRestoreTarget(
      InitializedDormantBackupRestoreTarget&& other) noexcept;
  InitializedDormantBackupRestoreTarget& operator=(
      InitializedDormantBackupRestoreTarget&& other) noexcept;
  InitializedDormantBackupRestoreTarget(
      const InitializedDormantBackupRestoreTarget&) = delete;
  InitializedDormantBackupRestoreTarget& operator=(
      const InitializedDormantBackupRestoreTarget&) = delete;
  ~InitializedDormantBackupRestoreTarget();

  std::unique_ptr<ProfileBackupRestoreTarget> target;
  PrecommitBackupRestoreCleanupHandle cleanup;
};

enum class BackupRestorePrecommitCleanupError : uint8_t {
  kInvalidHandle,
  kInvalidReceipt,
  kBusy,
  kRegistryRefused,
  kPersistenceFailed,
  kProfileStateRefused,
  kHistoryPresent,
  kStorageUnavailable,
  kRecoveryRequired,
};

}  // namespace taffy

#endif  // TAFFY_BROWSER_ANDROID_BROWSER_PROFILES_RESTORE_PRECOMMIT_CLEANUP_H_

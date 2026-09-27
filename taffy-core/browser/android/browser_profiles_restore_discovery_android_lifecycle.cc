// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <memory>
#include <utility>

#include "base/functional/bind.h"
#include "chrome/browser/profiles/profile.h"
#include "content/public/browser/browser_thread.h"
#include "taffy/browser/android/browser_profiles_restore_discovery_android_internal.h"
#include "taffy/browser/android/browser_profiles_restore_lifecycle.h"

namespace taffy {

void BrowserProfilesRestoreLifecycle::DiscoverInterruptedBackupRestore(
    Profile* source_profile,
    BackupRestoreRestartDiscoveryMode mode,
    RestartDiscoveryCallback callback) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!callback) {
    return;
  }
  if (!source_profile) {
    std::move(callback).Run(
        base::unexpected(BackupRestoreRestartDiscoveryError::kInvalidArgument));
    return;
  }
  if (operation_in_flight()) {
    std::move(callback).Run(
        base::unexpected(BackupRestoreRestartDiscoveryError::kBusy));
    return;
  }
  auto completion = base::BindOnce(
      [](base::WeakPtr<BrowserProfilesRestoreLifecycle> lifecycle,
         RestartDiscoveryCallback callback,
         BackupRestoreRestartDiscoveryResult result) {
        if (!lifecycle || !lifecycle->restart_discovery_) {
          return;
        }
        lifecycle->restart_discovery_.reset();
        std::move(callback).Run(std::move(result));
      },
      weak_factory_.GetWeakPtr(), std::move(callback));
  restart_discovery_ = std::make_unique<BackupRestoreRestartDiscoveryOperation>(
      profile_manager_, local_state_, source_profile->GetPath(), mode,
      std::move(completion));
  restart_discovery_->Start();
}

}  // namespace taffy

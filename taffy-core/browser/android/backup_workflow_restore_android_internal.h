// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_ANDROID_BACKUP_WORKFLOW_RESTORE_ANDROID_INTERNAL_H_
#define TAFFY_BROWSER_ANDROID_BACKUP_WORKFLOW_RESTORE_ANDROID_INTERNAL_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include "base/android/scoped_java_ref.h"
#include "taffy/browser/android/backup_workflow_restore_android.h"

namespace taffy {

struct BackupWorkflowRestoreAndroid::Operation {
  enum class Phase : uint8_t {
    kReserving,
    kBinding,
    kInitializing,
    kPlanning,
    kPresentationPersisting,
    kPreview,
    kStaging,
    kStaged,
    kCommitting,
    kHiddenReview,
    kCleanupReview,
    kResolving,
    kCleaning,
    kRecoveryRequired,
  };

  Operation(ProfileBackupWorkflow::WindowToken owner,
            std::string operation_id,
            std::u16string target_profile_label,
            ProfileManager* profile_manager,
            PrefService* local_state);
  Operation(const Operation&) = delete;
  Operation& operator=(const Operation&) = delete;
  ~Operation();

  const ProfileBackupWorkflow::WindowToken owner;
  const std::string operation_id;
  const std::u16string target_profile_label;
  Phase phase = Phase::kReserving;
  std::string reservation_id;
  std::string target_profile_id;
  ProfileBackupWorkflow::RestoreReviewToken review_token = 0;
  std::unique_ptr<BrowserProfilesRestoreLifecycle> creation_lifecycle;
  std::unique_ptr<BrowserProfilesRestoreLifecycle> resolution_lifecycle;
  std::optional<PrecommitBackupRestoreCleanupHandle> cleanup_handle;
  bool target_adopted = false;
  bool pretransfer_cleanup_started = false;
  bool discard_only_resolution = false;
  bool detached = false;
  bool precommit_cleanup_settled = false;
  bool commit_completion_pending = false;
  jni_zero::ScopedJavaGlobalRef<jobject> prepare_caller;
  jni_zero::ScopedJavaGlobalRef<jobject> stage_caller;
  jni_zero::ScopedJavaGlobalRef<jobject> commit_caller;
  jni_zero::ScopedJavaGlobalRef<jobject> resolution_caller;
};

struct BackupWorkflowRestoreAndroid::PendingRecoveryDiscovery {
  PendingRecoveryDiscovery(ProfileBackupWorkflow::WindowToken owner,
                           uint64_t request_token,
                           ProfileManager* profile_manager,
                           PrefService* local_state);
  PendingRecoveryDiscovery(const PendingRecoveryDiscovery&) = delete;
  PendingRecoveryDiscovery& operator=(const PendingRecoveryDiscovery&) = delete;
  ~PendingRecoveryDiscovery();

  const ProfileBackupWorkflow::WindowToken owner;
  const uint64_t request_token;
  std::unique_ptr<BrowserProfilesRestoreLifecycle> lifecycle;
  jni_zero::ScopedJavaGlobalRef<jobject> caller;
};

struct BackupWorkflowRestoreAndroid::RecoveredReview {
  enum class Phase : uint8_t { kReview, kResolving };

  RecoveredReview(ProfileBackupWorkflow::WindowToken owner,
                  uint64_t token,
                  std::string reservation_id,
                  bool discard_only);
  RecoveredReview(const RecoveredReview&) = delete;
  RecoveredReview& operator=(const RecoveredReview&) = delete;
  ~RecoveredReview();

  const ProfileBackupWorkflow::WindowToken owner;
  const uint64_t token;
  const std::string reservation_id;
  const bool discard_only;
  Phase phase = Phase::kReview;
  bool detached = false;
  std::unique_ptr<BrowserProfilesRestoreLifecycle> resolution_lifecycle;
  jni_zero::ScopedJavaGlobalRef<jobject> resolution_caller;
};

}  // namespace taffy

#endif  // TAFFY_BROWSER_ANDROID_BACKUP_WORKFLOW_RESTORE_ANDROID_INTERNAL_H_

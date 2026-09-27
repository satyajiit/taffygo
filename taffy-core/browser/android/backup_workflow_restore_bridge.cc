// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <jni.h>

#include <cstdint>
#include <string>
#include <vector>

#include "base/android/jni_array.h"
#include "base/android/jni_string.h"
#include "content/public/browser/browser_thread.h"
#include "taffy/browser/android/backup_workflow_android_notifications.h"
#include "taffy/browser/android/backup_workflow_bridge_internal.h"
#include "taffy/browser/android/backup_workflow_restore_android.h"

// Last on purpose: the generated header converts the @JniType parameters of
// this class's native methods, so the headers that declare those conversions
// have to be visible before it. jni_zero's own README states the rule.
#include "taffy/browser/android/backup_workflow_jni_headers/TaffyBackupRestoreWindow_jni.h"

namespace taffy {
namespace {

namespace mojom = core_service::mojom;

static_assert(
    static_cast<int>(ProfileBackupWorkflow::RestorePreparationStatus::kReady) ==
    0);
static_assert(static_cast<int>(
                  ProfileBackupWorkflow::RestorePreparationStatus::kRefused) ==
              1);
static_assert(
    static_cast<int>(
        ProfileBackupWorkflow::RestorePreparationStatus::kUnavailable) == 2);
static_assert(
    static_cast<int>(ProfileBackupWorkflow::RestoreStageStatus::kStaged) == 0);
static_assert(
    static_cast<int>(ProfileBackupWorkflow::RestoreStageStatus::kRefused) == 1);
static_assert(
    static_cast<int>(ProfileBackupWorkflow::RestoreStageStatus::kUnavailable) ==
    2);
static_assert(
    static_cast<int>(
        ProfileBackupWorkflow::RestoreCommitStatus::kHiddenCandidate) == 0);
static_assert(
    static_cast<int>(
        ProfileBackupWorkflow::RestoreCommitStatus::kDefinitelyNotCommitted) ==
    1);
static_assert(
    static_cast<int>(
        ProfileBackupWorkflow::RestoreCommitStatus::kRecoveryRequired) == 2);
static_assert(static_cast<int>(
                  ProfileBackupWorkflow::RestoreCommitStatus::kRefused) == 3);
static_assert(static_cast<int>(
                  ProfileBackupWorkflow::RestoreCommitStatus::kUnavailable) ==
              4);
static_assert(static_cast<int>(
                  ProfileBackupWorkflow::RestoreResolutionStatus::kPublished) ==
              0);
static_assert(
    static_cast<int>(
        ProfileBackupWorkflow::RestoreResolutionStatus::kVerifiedDeleted) == 1);
static_assert(static_cast<int>(ProfileBackupWorkflow::RestoreResolutionStatus::
                                   kDefinitelyNotCompleted) == 2);
static_assert(
    static_cast<int>(
        ProfileBackupWorkflow::RestoreResolutionStatus::kRecoveryRequired) ==
    3);
static_assert(static_cast<int>(
                  ProfileBackupWorkflow::RestoreResolutionStatus::kRefused) ==
              4);
static_assert(
    static_cast<int>(
        ProfileBackupWorkflow::RestoreResolutionStatus::kUnavailable) == 5);
static_assert(static_cast<int>(
                  mojom::BackupRestoreResolutionChoice::kAcceptCandidate) == 0);
static_assert(
    static_cast<int>(mojom::BackupRestoreResolutionChoice::kDiscardCandidate) ==
    1);
static_assert(static_cast<int>(BackupRestoreRestartDiscoveryStatus::kNone) ==
              0);
static_assert(
    static_cast<int>(BackupRestoreRestartDiscoveryStatus::kSourceUnavailable) ==
    1);
static_assert(
    static_cast<int>(BackupRestoreRestartDiscoveryStatus::kPrecommit) == 2);
static_assert(
    static_cast<int>(
        BackupRestoreRestartDiscoveryStatus::kPresentationUnavailable) == 3);
static_assert(static_cast<int>(
                  BackupRestoreRestartDiscoveryStatus::kSchemaMismatch) == 4);
static_assert(static_cast<int>(
                  BackupRestoreRestartDiscoveryStatus::kOutcomeUnknown) == 5);
static_assert(static_cast<int>(
                  BackupRestoreRestartDiscoveryStatus::kCustodyAmbiguous) == 6);
static_assert(
    static_cast<int>(BackupRestoreRestartDiscoveryStatus::kRollbackAvailable) ==
    7);
static_assert(static_cast<int>(
                  BackupRestoreRestartDiscoveryStatus::kCleanupRequired) == 8);
static_assert(
    static_cast<int>(BackupRestoreRestartDiscoveryStatus::kPublished) == 9);
static_assert(static_cast<int>(
                  BackupRestoreRestartDiscoveryStatus::kVerifiedDeleted) == 10);

BackupWorkflowAndroidBridge* Bridge(jlong pointer) {
  return reinterpret_cast<BackupWorkflowAndroidBridge*>(pointer);
}

}  // namespace

static jboolean JNI_TaffyBackupRestoreWindow_PrepareImportedRestore(
    JNIEnv* env,
    const jni_zero::JavaRef<jobject>& caller,
    jlong native_bridge,
    jlong window,
    const std::string& operation_id,
    const std::u16string& target_profile_label) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  BackupWorkflowAndroidBridge* bridge = Bridge(native_bridge);
  return bridge && bridge->Eligible() &&
         bridge->restore()->Prepare(caller, static_cast<uint64_t>(window),
                                    operation_id, target_profile_label);
}

static jboolean JNI_TaffyBackupRestoreWindow_ConfirmAndStageRestore(
    JNIEnv* env,
    const jni_zero::JavaRef<jobject>& caller,
    jlong native_bridge,
    jlong window,
    jlong review_token) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  BackupWorkflowAndroidBridge* bridge = Bridge(native_bridge);
  return bridge && bridge->Eligible() && review_token > 0 &&
         bridge->restore()->ConfirmAndStage(
             caller, static_cast<uint64_t>(window),
             static_cast<uint64_t>(review_token));
}

static jboolean JNI_TaffyBackupRestoreWindow_CommitRestore(
    JNIEnv* env,
    const jni_zero::JavaRef<jobject>& caller,
    jlong native_bridge,
    jlong window,
    jlong review_token) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  BackupWorkflowAndroidBridge* bridge = Bridge(native_bridge);
  return bridge && bridge->Eligible() && review_token > 0 &&
         bridge->restore()->Commit(caller, static_cast<uint64_t>(window),
                                   static_cast<uint64_t>(review_token));
}

static jboolean JNI_TaffyBackupRestoreWindow_ResolveRestore(
    JNIEnv* env,
    const jni_zero::JavaRef<jobject>& caller,
    jlong native_bridge,
    jlong window,
    jlong review_token,
    jint choice) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  BackupWorkflowAndroidBridge* bridge = Bridge(native_bridge);
  if (!bridge || !bridge->Eligible() || review_token <= 0 || choice < 0 ||
      choice > 1) {
    return false;
  }
  return bridge->restore()->Resolve(
      caller, static_cast<uint64_t>(window),
      static_cast<uint64_t>(review_token),
      static_cast<mojom::BackupRestoreResolutionChoice>(choice));
}

static jboolean JNI_TaffyBackupRestoreWindow_DiscoverInterruptedRestore(
    JNIEnv* env,
    const jni_zero::JavaRef<jobject>& caller,
    jlong native_bridge,
    jlong window,
    jlong request_token) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  BackupWorkflowAndroidBridge* bridge = Bridge(native_bridge);
  return bridge && bridge->Eligible() && request_token > 0 &&
         bridge->restore()->DiscoverInterruptedRestore(
             caller, static_cast<uint64_t>(window),
             static_cast<uint64_t>(request_token));
}

static void JNI_TaffyBackupRestoreWindow_AbandonInterruptedRestoreDiscovery(
    JNIEnv* env,
    jlong native_bridge,
    jlong window,
    jlong request_token) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (BackupWorkflowAndroidBridge* bridge = Bridge(native_bridge);
      bridge && request_token > 0) {
    bridge->restore()->AbandonInterruptedRestoreDiscovery(
        static_cast<uint64_t>(window), static_cast<uint64_t>(request_token));
  }
}

static jboolean JNI_TaffyBackupRestoreWindow_ResolveRecoveredRestore(
    JNIEnv* env,
    const jni_zero::JavaRef<jobject>& caller,
    jlong native_bridge,
    jlong window,
    jlong recovered_review_token,
    jint choice) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  BackupWorkflowAndroidBridge* bridge = Bridge(native_bridge);
  if (!bridge || !bridge->Eligible() || recovered_review_token <= 0 ||
      choice < 0 || choice > 1) {
    return false;
  }
  return bridge->restore()->ResolveRecoveredRestore(
      caller, static_cast<uint64_t>(window),
      static_cast<uint64_t>(recovered_review_token),
      static_cast<mojom::BackupRestoreResolutionChoice>(choice));
}

static void JNI_TaffyBackupRestoreWindow_AbandonRecoveredRestoreReview(
    JNIEnv* env,
    jlong native_bridge,
    jlong window,
    jlong recovered_review_token) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (BackupWorkflowAndroidBridge* bridge = Bridge(native_bridge);
      bridge && recovered_review_token > 0) {
    bridge->restore()->AbandonRecoveredRestoreReview(
        static_cast<uint64_t>(window),
        static_cast<uint64_t>(recovered_review_token));
  }
}

DEFINE_JNI(TaffyBackupRestoreWindow)

namespace backup_workflow_notifications {

void RestorePrepared(const jni_zero::JavaRef<jobject>& caller,
                     const std::string& operation_id,
                     int64_t review_token,
                     const std::u16string& target_profile_label,
                     const std::vector<int32_t>& class_counts,
                     bool has_conflicts,
                     bool can_stage,
                     int32_t status) {
  if (!caller) {
    return;
  }
  JNIEnv* env = jni_zero::AttachCurrentThread();
  Java_TaffyBackupRestoreWindow_onRestorePrepared(
      env, caller, base::android::ConvertUTF8ToJavaString(env, operation_id),
      static_cast<jlong>(review_token),
      base::android::ConvertUTF16ToJavaString(env, target_profile_label),
      base::android::ToJavaIntArray(env, class_counts), has_conflicts,
      can_stage, static_cast<jint>(status));
}

void RestoreStaged(const jni_zero::JavaRef<jobject>& caller,
                   int64_t review_token,
                   int32_t status) {
  if (caller) {
    Java_TaffyBackupRestoreWindow_onRestoreStaged(
        jni_zero::AttachCurrentThread(), caller,
        static_cast<jlong>(review_token), static_cast<jint>(status));
  }
}

void RestoreCommitted(const jni_zero::JavaRef<jobject>& caller,
                      int64_t review_token,
                      int32_t status) {
  if (caller) {
    Java_TaffyBackupRestoreWindow_onRestoreCommitted(
        jni_zero::AttachCurrentThread(), caller,
        static_cast<jlong>(review_token), static_cast<jint>(status));
  }
}

void RestoreResolved(const jni_zero::JavaRef<jobject>& caller,
                     int64_t review_token,
                     int32_t status) {
  if (caller) {
    Java_TaffyBackupRestoreWindow_onRestoreResolved(
        jni_zero::AttachCurrentThread(), caller,
        static_cast<jlong>(review_token), static_cast<jint>(status));
  }
}

void InterruptedRestoreDiscovered(const jni_zero::JavaRef<jobject>& caller,
                                  int64_t request_token,
                                  int64_t review_token,
                                  const std::u16string& target_profile_label,
                                  const std::vector<int32_t>& class_counts,
                                  bool has_conflicts,
                                  bool can_stage,
                                  bool cleanup_only,
                                  int32_t status) {
  if (!caller) {
    return;
  }
  JNIEnv* env = jni_zero::AttachCurrentThread();
  Java_TaffyBackupRestoreWindow_onInterruptedRestoreDiscovered(
      env, caller, static_cast<jlong>(request_token),
      static_cast<jlong>(review_token),
      base::android::ConvertUTF16ToJavaString(env, target_profile_label),
      base::android::ToJavaIntArray(env, class_counts), has_conflicts,
      can_stage, cleanup_only, static_cast<jint>(status));
}

void RecoveredRestoreResolved(const jni_zero::JavaRef<jobject>& caller,
                              int64_t token,
                              int32_t status) {
  if (caller) {
    Java_TaffyBackupRestoreWindow_onRecoveredRestoreResolved(
        jni_zero::AttachCurrentThread(), caller, static_cast<jlong>(token),
        static_cast<jint>(status));
  }
}

}  // namespace backup_workflow_notifications

}  // namespace taffy

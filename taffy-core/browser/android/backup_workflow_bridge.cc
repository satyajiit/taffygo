// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <jni.h>

#include <algorithm>
#include <array>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/android/jni_string.h"
#include "base/android/scoped_java_ref.h"
#include "base/containers/span.h"
#include "base/files/file.h"
#include "base/numerics/safe_conversions.h"
#include "chrome/browser/profiles/profile.h"
#include "content/public/browser/browser_thread.h"
#include "crypto/secure_util.h"
#include "taffy/browser/android/backup_workflow_android_notifications.h"
#include "taffy/browser/android/backup_workflow_bridge_internal.h"
#include "taffy/browser/profile_backup_workflow.h"
#include "taffy/browser/profile_backup_workflow_io.h"
#include "taffy/contracts/core-service/generated/cpp/core_service_enums.h"

// Last on purpose: the generated header converts the @JniType parameters of
// this class's native methods, so the headers that declare those conversions
// have to be visible before it. jni_zero's own README states the rule.
#include "taffy/browser/android/backup_workflow_jni_headers/TaffyBackupWorkflowBridge_jni.h"

namespace taffy {
namespace {

namespace core_mojom = core_service::mojom;

static_assert(static_cast<int>(ProfileBackupWorkflow::KeyMode::kCreate) == 0);
static_assert(static_cast<int>(ProfileBackupWorkflow::KeyMode::kRestore) == 1);
static_assert(
    static_cast<int>(ProfileBackupWorkflow::KeyAcceptance::kAccepted) == 0);
static_assert(
    static_cast<int>(ProfileBackupWorkflow::KeyAcceptance::kRefused) == 1);
static_assert(
    static_cast<int>(ProfileBackupWorkflow::KeyAcceptance::kUnavailable) == 2);
static_assert(static_cast<int>(
                  ProfileBackupWorkflow::ExportPreparationStatus::kReady) == 0);
static_assert(static_cast<int>(
                  ProfileBackupWorkflow::ExportPreparationStatus::kRefused) ==
              1);
static_assert(
    static_cast<int>(
        ProfileBackupWorkflow::ExportPreparationStatus::kUnavailable) == 2);
static_assert(static_cast<int>(storage::backup::BackupStageStatus::kVerified) ==
              0);
static_assert(static_cast<int>(storage::backup::BackupStageStatus::kRefused) ==
              1);
static_assert(
    static_cast<int>(storage::backup::BackupStageStatus::kUnavailable) == 2);

BackupWorkflowAndroidBridge* Bridge(jlong pointer) {
  return reinterpret_cast<BackupWorkflowAndroidBridge*>(pointer);
}

ProfileBackupWorkflowIoHandle* IoHandle(jlong pointer) {
  return reinterpret_cast<ProfileBackupWorkflowIoHandle*>(pointer);
}

jint DetachFile(base::File file) {
  return file.IsValid() ? static_cast<jint>(file.TakePlatformFile()) : -1;
}

}  // namespace

static jlong JNI_TaffyBackupWorkflowBridge_Init(
    JNIEnv* env,
    const jni_zero::JavaRef<jobject>& caller,
    Profile* profile) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  return reinterpret_cast<jlong>(
      BackupWorkflowAndroidBridge::Create(caller, profile).release());
}

static void JNI_TaffyBackupWorkflowBridge_Destroy(JNIEnv* env,
                                                  jlong native_bridge) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  delete Bridge(native_bridge);
}

static jlong JNI_TaffyBackupWorkflowBridge_RegisterWindow(JNIEnv* env,
                                                          jlong native_bridge) {
  BackupWorkflowAndroidBridge* bridge = Bridge(native_bridge);
  return bridge && bridge->Eligible()
             ? static_cast<jlong>(bridge->workflow()->RegisterWindow())
             : 0;
}

static void JNI_TaffyBackupWorkflowBridge_UnregisterWindow(JNIEnv* env,
                                                           jlong native_bridge,
                                                           jlong window) {
  if (BackupWorkflowAndroidBridge* bridge = Bridge(native_bridge)) {
    bridge->UnregisterWindow(static_cast<uint64_t>(window));
  }
}

static jboolean JNI_TaffyBackupWorkflowBridge_Activate(JNIEnv* env,
                                                       jlong native_bridge,
                                                       jlong window) {
  BackupWorkflowAndroidBridge* bridge = Bridge(native_bridge);
  return bridge && bridge->Eligible() &&
         bridge->workflow()->ActivateWindow(static_cast<uint64_t>(window));
}

static void JNI_TaffyBackupWorkflowBridge_Deactivate(JNIEnv* env,
                                                     jlong native_bridge,
                                                     jlong window) {
  if (BackupWorkflowAndroidBridge* bridge = Bridge(native_bridge)) {
    bridge->workflow()->DeactivateWindow(static_cast<uint64_t>(window));
  }
}

static jni_zero::ScopedJavaLocalRef<jstring>
JNI_TaffyBackupWorkflowBridge_BeginRecoveryKeySession(JNIEnv* env,
                                                      jlong native_bridge,
                                                      jlong window,
                                                      jint mode) {
  BackupWorkflowAndroidBridge* bridge = Bridge(native_bridge);
  if (!bridge || !bridge->Eligible() || mode < 0 || mode > 1) {
    return {};
  }
  auto operation = bridge->workflow()->OpenRecoveryKeySession(
      static_cast<uint64_t>(window),
      static_cast<ProfileBackupWorkflow::KeyMode>(mode));
  return operation ? base::android::ConvertUTF8ToJavaString(env, *operation)
                   : jni_zero::ScopedJavaLocalRef<jstring>();
}

static jni_zero::ScopedJavaLocalRef<jcharArray>
JNI_TaffyBackupWorkflowBridge_TakeGeneratedKeyForDisplay(
    JNIEnv* env,
    jlong native_bridge,
    jlong window,
    const std::string& operation_id) {
  BackupWorkflowAndroidBridge* bridge = Bridge(native_bridge);
  auto text = bridge && bridge->Eligible()
                  ? bridge->workflow()->TakeGeneratedKeyForDisplay(
                        static_cast<uint64_t>(window), operation_id)
                  : std::nullopt;
  if (!text) {
    return {};
  }
  std::array<jchar, storage::backup::kRecoveryKeyTextChars> java_chars{};
  std::ranges::copy(*text, java_chars.begin());
  const jsize text_length = base::checked_cast<jsize>(java_chars.size());
  jcharArray array = env->NewCharArray(text_length);
  if (array) {
    env->SetCharArrayRegion(array, 0, text_length, java_chars.data());
  }
  // The key text is UTF-16 and SecureZeroBuffer wipes a byte span, so both
  // copies are handed over as writable bytes rather than as characters.
  crypto::SecureZeroBuffer(base::as_writable_byte_span(*text));
  crypto::SecureZeroBuffer(base::as_writable_byte_span(java_chars));
  return jni_zero::ScopedJavaLocalRef<jcharArray>::Adopt(env, array);
}

static jint JNI_TaffyBackupWorkflowBridge_ConfirmKeyRetained(
    JNIEnv* env,
    jlong native_bridge,
    jlong window,
    const std::string& operation_id) {
  BackupWorkflowAndroidBridge* bridge = Bridge(native_bridge);
  return static_cast<jint>(
      bridge && bridge->Eligible()
          ? bridge->workflow()->ConfirmKeyRetained(
                static_cast<uint64_t>(window), operation_id)
          : ProfileBackupWorkflow::KeyAcceptance::kUnavailable);
}

static jint JNI_TaffyBackupWorkflowBridge_AcceptEnteredKey(
    JNIEnv* env,
    jlong native_bridge,
    jlong window,
    const std::string& operation_id,
    const jni_zero::JavaRef<jcharArray>& entered_key) {
  BackupWorkflowAndroidBridge* bridge = Bridge(native_bridge);
  if (!bridge || !bridge->Eligible() || !entered_key) {
    return static_cast<jint>(
        ProfileBackupWorkflow::KeyAcceptance::kUnavailable);
  }
  const jsize text_length =
      base::checked_cast<jsize>(storage::backup::kRecoveryKeyTextChars);
  if (env->GetArrayLength(entered_key.obj()) != text_length) {
    return static_cast<jint>(ProfileBackupWorkflow::KeyAcceptance::kRefused);
  }
  std::array<jchar, storage::backup::kRecoveryKeyTextChars> java_chars{};
  storage::backup::RecoveryKeyText text{};
  env->GetCharArrayRegion(entered_key.obj(), 0, text_length, java_chars.data());
  std::ranges::copy(java_chars, text.begin());
  const ProfileBackupWorkflow::KeyAcceptance accepted =
      bridge->workflow()->AcceptEnteredKey(static_cast<uint64_t>(window),
                                           operation_id, text);
  // Both buffers hold UTF-16 characters; wipe them as bytes.
  crypto::SecureZeroBuffer(base::as_writable_byte_span(text));
  crypto::SecureZeroBuffer(base::as_writable_byte_span(java_chars));
  return static_cast<jint>(accepted);
}

static jboolean JNI_TaffyBackupWorkflowBridge_PrepareExport(
    JNIEnv* env,
    jlong native_bridge,
    jlong window,
    const std::string& operation_id,
    const std::vector<int32_t>& wire_selection) {
  BackupWorkflowAndroidBridge* bridge = Bridge(native_bridge);
  auto selection =
      backup_workflow_internal::DecodeSelection(wire_selection);
  if (!bridge || !bridge->Eligible() || !selection) {
    return false;
  }
  return bridge->workflow()->PrepareExport(
      static_cast<uint64_t>(window), operation_id, std::move(*selection),
      base::BindOnce(&BackupWorkflowAndroidBridge::OnExportPrepared,
                     bridge->GetWeakPtr(), static_cast<uint64_t>(window),
                     operation_id));
}

static jlong JNI_TaffyBackupWorkflowBridge_MaximumImportBytes(
    JNIEnv* env,
    jlong native_bridge,
    jlong window,
    const std::string& operation_id) {
  BackupWorkflowAndroidBridge* bridge = Bridge(native_bridge);
  auto maximum = bridge ? bridge->workflow()->MaximumImportBytes(
                              static_cast<uint64_t>(window), operation_id)
                        : std::nullopt;
  return maximum && *maximum <=
                        static_cast<uint64_t>(std::numeric_limits<jlong>::max())
             ? static_cast<jlong>(*maximum)
             : -1;
}

static jint JNI_TaffyBackupWorkflowBridge_OpenEncryptedArchiveReadFd(
    JNIEnv* env,
    jlong native_bridge,
    jlong window,
    const std::string& operation_id) {
  BackupWorkflowAndroidBridge* bridge = Bridge(native_bridge);
  return bridge ? DetachFile(bridge->workflow()->OpenEncryptedArchiveRead(
                      static_cast<uint64_t>(window), operation_id))
                : -1;
}

static jint JNI_TaffyBackupWorkflowBridge_OpenReadbackWriteFd(
    JNIEnv* env,
    jlong native_bridge,
    jlong window,
    const std::string& operation_id,
    jlong expected_bytes) {
  BackupWorkflowAndroidBridge* bridge = Bridge(native_bridge);
  return bridge && expected_bytes > 0
             ? DetachFile(bridge->workflow()->OpenExportReadback(
                   static_cast<uint64_t>(window), operation_id,
                   static_cast<uint64_t>(expected_bytes)))
             : -1;
}

static jlong JNI_TaffyBackupWorkflowBridge_AcquireExportVerification(
    JNIEnv* env,
    jlong native_bridge,
    jlong window,
    const std::string& operation_id) {
  BackupWorkflowAndroidBridge* bridge = Bridge(native_bridge);
  return reinterpret_cast<jlong>(
      bridge ? bridge->workflow()
                   ->AcquireExportVerification(static_cast<uint64_t>(window),
                                               operation_id)
                   .release()
             : nullptr);
}

static jint JNI_TaffyBackupWorkflowBridge_OpenEncryptedImportWriteFd(
    JNIEnv* env,
    jlong native_bridge,
    jlong window,
    const std::string& operation_id,
    jlong maximum_bytes) {
  BackupWorkflowAndroidBridge* bridge = Bridge(native_bridge);
  return bridge && maximum_bytes > 0
             ? DetachFile(bridge->workflow()->OpenEncryptedImport(
                   static_cast<uint64_t>(window), operation_id,
                   static_cast<uint64_t>(maximum_bytes)))
             : -1;
}

static jlong JNI_TaffyBackupWorkflowBridge_AcquireImportInspection(
    JNIEnv* env,
    jlong native_bridge,
    jlong window,
    const std::string& operation_id,
    jlong actual_bytes) {
  BackupWorkflowAndroidBridge* bridge = Bridge(native_bridge);
  return reinterpret_cast<jlong>(
      bridge && actual_bytes > 0
          ? bridge->workflow()
                ->AcquireImportInspection(static_cast<uint64_t>(window),
                                          operation_id,
                                          static_cast<uint64_t>(actual_bytes))
                .release()
          : nullptr);
}

static void JNI_TaffyBackupWorkflowBridge_RunArchiveIo(JNIEnv* env,
                                                       jlong io_handle) {
  if (content::BrowserThread::CurrentlyOn(content::BrowserThread::UI)) {
    return;
  }
  if (ProfileBackupWorkflowIoHandle* handle = IoHandle(io_handle)) {
    handle->Run();
  }
}

static jint JNI_TaffyBackupWorkflowBridge_CompleteExportVerification(
    JNIEnv* env,
    jlong native_bridge,
    jlong window,
    const std::string& operation_id,
    jlong io_handle) {
  BackupWorkflowAndroidBridge* bridge = Bridge(native_bridge);
  ProfileBackupWorkflowIoHandle* handle = IoHandle(io_handle);
  return static_cast<jint>(
      bridge && handle
          ? bridge->workflow()->CompleteExportVerification(
                static_cast<uint64_t>(window), operation_id, *handle)
          : storage::backup::BackupStageStatus::kUnavailable);
}

static jint JNI_TaffyBackupWorkflowBridge_CompleteImportInspection(
    JNIEnv* env,
    jlong native_bridge,
    jlong window,
    const std::string& operation_id,
    jlong io_handle) {
  BackupWorkflowAndroidBridge* bridge = Bridge(native_bridge);
  ProfileBackupWorkflowIoHandle* handle = IoHandle(io_handle);
  return static_cast<jint>(
      bridge && handle
          ? bridge->workflow()->CompleteImportInspection(
                static_cast<uint64_t>(window), operation_id, *handle)
          : storage::backup::BackupStageStatus::kUnavailable);
}

static void JNI_TaffyBackupWorkflowBridge_DestroyArchiveIo(JNIEnv* env,
                                                           jlong io_handle) {
  delete IoHandle(io_handle);
}

static void JNI_TaffyBackupWorkflowBridge_AbandonOperation(
    JNIEnv* env,
    jlong native_bridge,
    jlong window,
    const std::string& operation_id) {
  if (BackupWorkflowAndroidBridge* bridge = Bridge(native_bridge)) {
    bridge->AbandonOperation(static_cast<uint64_t>(window), operation_id);
  }
}

DEFINE_JNI(TaffyBackupWorkflowBridge)

namespace backup_workflow_notifications {

void ExportPrepared(const jni_zero::JavaRef<jobject>& caller,
                    int64_t window,
                    const std::string& operation_id,
                    int64_t archive_bytes,
                    int32_t status) {
  if (!caller) {
    return;
  }
  JNIEnv* env = jni_zero::AttachCurrentThread();
  Java_TaffyBackupWorkflowBridge_onExportPrepared(
      env, caller, static_cast<jlong>(window),
      base::android::ConvertUTF8ToJavaString(env, operation_id),
      static_cast<jlong>(archive_bytes), static_cast<jint>(status));
}

}  // namespace backup_workflow_notifications

}  // namespace taffy

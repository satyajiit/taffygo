// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_ANDROID_BACKUP_WORKFLOW_ANDROID_NOTIFICATIONS_H_
#define TAFFY_BROWSER_ANDROID_BACKUP_WORKFLOW_ANDROID_NOTIFICATIONS_H_

#include <cstdint>
#include <string>
#include <vector>

#include "base/android/scoped_java_ref.h"

namespace taffy::backup_workflow_notifications {

// The browser-to-Java half of the two backup JNI classes, behind one seam.
//
// jni_zero generates a `<Class>_jni.h` that is a *single translation unit*
// header: it emits `YouForgotToCallMacro_DEFINE_JNI_<Class>`, a static function
// that only `DEFINE_JNI(<Class>)` calls, so any second file that includes the
// header fails the build under -Wunused-function. The exact spelling of that
// failure is a warning about an unused function nobody wrote, in a generated
// file, which reads like a toolchain fault rather than the layering rule it is.
//
// So exactly one file per class includes the generated header: the file that
// implements that class's native methods and calls DEFINE_JNI —
// backup_workflow_restore_bridge.cc for TaffyBackupRestoreWindow and
// backup_workflow_bridge.cc for TaffyBackupWorkflowBridge. Every other
// translation unit that has something to tell Java calls one of the functions
// below instead. A new browser-to-Java message is one declaration here and one
// definition beside its DEFINE_JNI, never a second include of the generated
// header.
//
// These carry already-projected values on purpose. Deciding what to send —
// flattening a class summary, collapsing a status the surface must not see — is
// domain logic and stays with the code that owns it; this seam only converts
// and calls. Every function is a no-op when `caller` is null, because a window
// can be released while work it started is still in flight.

// TaffyBackupRestoreWindow.
void RestorePrepared(const jni_zero::JavaRef<jobject>& caller,
                     const std::string& operation_id,
                     int64_t review_token,
                     const std::u16string& target_profile_label,
                     const std::vector<int32_t>& class_counts,
                     bool has_conflicts,
                     bool can_stage,
                     int32_t status);
void RestoreStaged(const jni_zero::JavaRef<jobject>& caller,
                   int64_t review_token,
                   int32_t status);
void RestoreCommitted(const jni_zero::JavaRef<jobject>& caller,
                      int64_t review_token,
                      int32_t status);
void RestoreResolved(const jni_zero::JavaRef<jobject>& caller,
                     int64_t review_token,
                     int32_t status);
void InterruptedRestoreDiscovered(const jni_zero::JavaRef<jobject>& caller,
                                  int64_t request_token,
                                  int64_t review_token,
                                  const std::u16string& target_profile_label,
                                  const std::vector<int32_t>& class_counts,
                                  bool has_conflicts,
                                  bool can_stage,
                                  bool cleanup_only,
                                  int32_t status);
void RecoveredRestoreResolved(const jni_zero::JavaRef<jobject>& caller,
                              int64_t token,
                              int32_t status);

// TaffyBackupWorkflowBridge.
void ExportPrepared(const jni_zero::JavaRef<jobject>& caller,
                    int64_t window,
                    const std::string& operation_id,
                    int64_t archive_bytes,
                    int32_t status);

}  // namespace taffy::backup_workflow_notifications

#endif  // TAFFY_BROWSER_ANDROID_BACKUP_WORKFLOW_ANDROID_NOTIFICATIONS_H_

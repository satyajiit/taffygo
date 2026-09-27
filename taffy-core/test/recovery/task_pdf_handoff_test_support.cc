// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/test/recovery/task_pdf_handoff_test_support.h"

#include <utility>

#include "base/android/callback_android.h"
#include "base/android/jni_string.h"
#include "chrome/browser/profiles/profile.h"
#include "ui/android/window_android.h"

// Keep after the headers supplying JNI conversions.
#include "taffy/test/recovery/task_pdf_handoff_jni_headers/TaffyTaskPdfTestBridge_jni.h"

namespace taffy::test {

void StartTaskPdfHandoff(Profile* profile,
                         ui::WindowAndroid* window,
                         std::string_view task_id,
                         std::string_view download_guid,
                         base::OnceCallback<void(bool)> callback) {
  if (!profile || !window) {
    std::move(callback).Run(false);
    return;
  }
  JNIEnv* env = jni_zero::AttachCurrentThread();
  Java_TaffyTaskPdfTestBridge_start(
      env, profile->GetJavaObject(), window->GetJavaObject(),
      base::android::ConvertUTF8ToJavaString(env, task_id),
      base::android::ConvertUTF8ToJavaString(env, download_guid),
      base::android::ToJniCallback(env, std::move(callback)));
}

}  // namespace taffy::test

DEFINE_JNI(TaffyTaskPdfTestBridge)

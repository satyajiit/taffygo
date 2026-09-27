// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <jni.h>

#include <string>

#include "base/android/jni_string.h"
#include "chrome/browser/profiles/profile.h"
#include "mojo/public/cpp/system/message_pipe.h"
#include "taffy/browser/account/profile_platform_adapter.mojom.h"
#include "taffy/browser/android/profile_platform_jni_headers/TaffyProfilePlatformBridge_jni.h"
#include "taffy/browser/core_service_manager.h"
#include "taffy/browser/core_service_manager_factory.h"

namespace taffy {

static jboolean JNI_TaffyProfilePlatformBridge_BindPlatformAdapter(
    JNIEnv *env, const jni_zero::JavaRef<jobject> &java_profile,
    int64_t message_pipe_handle) {
  Profile *profile = Profile::FromJavaObject(java_profile);
  if (!profile) {
    return false;
  }
  CoreServiceManager *manager =
      CoreServiceManagerFactory::GetForProfile(profile);
  mojo::ScopedMessagePipeHandle pipe{
      mojo::MessagePipeHandle(message_pipe_handle)};
  mojo::PendingRemote<browser::account::mojom::TaffyProfilePlatformAdapter>
      adapter(std::move(pipe),
              browser::account::mojom::TaffyProfilePlatformAdapter::Version_);
  if (!manager || !adapter.is_valid()) {
    return false;
  }
  manager->BindPlatformAdapter(std::move(adapter));
  return true;
}

static jboolean JNI_TaffyProfilePlatformBridge_DeliverAuthCallback(
    JNIEnv *env, const jni_zero::JavaRef<jobject> &java_profile,
    const jni_zero::JavaRef<jstring> &raw_uri) {
  Profile *profile = Profile::FromJavaObject(java_profile);
  if (!profile || !raw_uri) {
    return false;
  }
  CoreServiceManager *manager =
      CoreServiceManagerFactory::GetForProfileIfExists(profile);
  if (!manager) {
    return false;
  }
  return manager->DeliverAuthCallback(
      base::android::ConvertJavaStringToUTF8(env, raw_uri));
}

// The manual-code fallback's browser entry (decision 0095 section 2).
// Interception depends on a vendor keeping a redirect address the product
// recognises, and a vendor may change one; a flow with no fallback breaks
// silently the day that happens. What arrives here is what the vendor
// displayed and the person typed on the product's own chrome — for some
// vendors the code and the state joined by a `#`, which is why this is
// handed over whole rather than split by Java. False means no live flow
// accepted it, and the person may simply try again.
static jboolean JNI_TaffyProfilePlatformBridge_SubmitProviderAuthCode(
    JNIEnv *env, const jni_zero::JavaRef<jobject> &java_profile,
    const jni_zero::JavaRef<jstring> &flow_id,
    const jni_zero::JavaRef<jstring> &code) {
  Profile *profile = Profile::FromJavaObject(java_profile);
  if (!profile || !flow_id || !code) {
    return false;
  }
  CoreServiceManager *manager =
      CoreServiceManagerFactory::GetForProfileIfExists(profile);
  if (!manager) {
    return false;
  }
  return manager->SubmitProviderAuthCode(
      base::android::ConvertJavaStringToUTF8(env, flow_id),
      base::android::ConvertJavaStringToUTF8(env, code));
}

// Best-effort by design (decision 0081): the sealed record is already gone
// whatever the vendor answers, so this returns nothing and the caller waits
// on nothing.
static void JNI_TaffyProfilePlatformBridge_RevokeProviderCredential(
    JNIEnv *env, const jni_zero::JavaRef<jobject> &java_profile,
    const jni_zero::JavaRef<jstring> &provider_id,
    const jni_zero::JavaRef<jstring> &token) {
  Profile *profile = Profile::FromJavaObject(java_profile);
  if (!profile || !provider_id || !token) {
    return;
  }
  CoreServiceManager *manager =
      CoreServiceManagerFactory::GetForProfileIfExists(profile);
  if (!manager) {
    return;
  }
  manager->RevokeProviderCredential(
      base::android::ConvertJavaStringToUTF8(env, provider_id),
      base::android::ConvertJavaStringToUTF8(env, token));
}

DEFINE_JNI(TaffyProfilePlatformBridge)

} // namespace taffy

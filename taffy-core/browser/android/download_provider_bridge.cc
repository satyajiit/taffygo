// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <jni.h>

#include <string>

#include "base/android/jni_string.h"
#include "base/android/scoped_java_ref.h"
#include "chrome/browser/offline_items_collection/offline_content_aggregator_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_key.h"
#include "components/offline_items_collection/core/android/offline_content_aggregator_bridge.h"
#include "taffy/browser/android/download_provider_jni_headers/TaffyDownloadProviderBridge_jni.h"
#include "taffy/browser/core_service_manager.h"
#include "taffy/browser/core_service_manager_factory.h"

namespace taffy {

static base::android::ScopedJavaLocalRef<jobject>
JNI_TaffyDownloadProviderBridge_GetForProfile(
    JNIEnv* env,
    const jni_zero::JavaRef<jobject>& java_profile) {
  Profile* profile = Profile::FromJavaObject(java_profile);
  if (!profile || profile->IsOffTheRecord()) {
    return {};
  }
  auto* aggregator =
      OfflineContentAggregatorFactory::GetForKey(profile->GetProfileKey());
  if (!aggregator) {
    return {};
  }
  return offline_items_collection::android::OfflineContentAggregatorBridge::
      GetBridgeForOfflineContentAggregator(aggregator);
}

static bool JNI_TaffyDownloadProviderBridge_CanOpenTaskDownload(
    JNIEnv* env,
    const jni_zero::JavaRef<jobject>& java_profile,
    const std::string& task_id,
    const std::string& download_id) {
  Profile* profile = Profile::FromJavaObject(java_profile);
  if (!profile || profile->IsOffTheRecord()) {
    return false;
  }
  auto* manager = CoreServiceManagerFactory::GetForProfileIfExists(profile);
  return manager && manager->CanOpenTaskDownloadForPerson(task_id, download_id);
}

DEFINE_JNI(TaffyDownloadProviderBridge)

}  // namespace taffy

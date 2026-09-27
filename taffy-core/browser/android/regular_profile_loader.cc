// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <jni.h>

#include <string>
#include <utility>

#include "base/android/jni_android.h"
#include "base/android/jni_string.h"
#include "base/android/scoped_java_ref.h"
#include "base/files/file_path.h"
#include "base/functional/bind.h"
#include "base/path_service.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/nuke_profile_directory_utils.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_attributes_entry.h"
#include "chrome/browser/profiles/profile_attributes_storage.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/common/chrome_paths.h"
#include "content/public/browser/browser_thread.h"
#include "taffy/browser/android/regular_profile_loader_jni_headers/TaffyRegularProfileLoader_jni.h"
#include "taffy/browser/android/regular_profile_token.h"

namespace taffy {
namespace {

ProfileManager* Manager() {
  return g_browser_process ? g_browser_process->profile_manager() : nullptr;
}

bool IsDurableRegularEntry(ProfileManager& manager,
                           ProfileAttributesEntry& entry) {
  const base::FilePath path = entry.GetPath();
  return !path.empty() && path != ProfileManager::GetGuestProfilePath() &&
         !entry.IsOmitted() && !entry.IsEphemeral() &&
         !IsProfileDirectoryMarkedForDeletion(path) &&
         manager.IsAllowedProfilePath(path);
}

std::string TokenForPath(const base::FilePath& user_data_dir,
                         const base::FilePath& profile_path) {
  base::FilePath relative_path;
  if (!user_data_dir.AppendRelativePath(profile_path, &relative_path)) {
    return {};
  }
  return OpaqueRegularProfileToken(relative_path.AsUTF8Unsafe());
}

void Complete(jlong request_id, Profile* profile) {
  JNIEnv* env = jni_zero::AttachCurrentThread();
  Java_TaffyRegularProfileLoader_onLoadCompleted(
      env, request_id,
      profile ? profile->GetJavaObject()
              : jni_zero::ScopedJavaLocalRef<jobject>());
}

void OnProfileLoaded(jlong request_id,
                     base::FilePath expected_path,
                     Profile* profile) {
  ProfileManager* manager = Manager();
  if (!profile || !manager || !profile->IsRegularProfile() ||
      profile->GetPath() != expected_path || !manager->IsValidProfile(profile)) {
    Complete(request_id, nullptr);
    return;
  }
  ProfileAttributesEntry* entry =
      manager->GetProfileAttributesStorage().GetProfileAttributesWithPath(
          expected_path);
  Complete(request_id,
           entry && IsDurableRegularEntry(*manager, *entry) ? profile
                                                            : nullptr);
}

}  // namespace

static void JNI_TaffyRegularProfileLoader_Load(
    JNIEnv* env,
    const jni_zero::JavaRef<jstring>& java_opaque_token,
    jlong request_id) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (request_id <= 0 || !java_opaque_token) {
    Complete(request_id, nullptr);
    return;
  }
  const std::string opaque_token =
      base::android::ConvertJavaStringToUTF8(env, java_opaque_token);
  ProfileManager* manager = Manager();
  base::FilePath user_data_dir;
  if (!manager || !IsValidOpaqueRegularProfileToken(opaque_token) ||
      !base::PathService::Get(chrome::DIR_USER_DATA, &user_data_dir)) {
    Complete(request_id, nullptr);
    return;
  }

  base::FilePath matched_path;
  for (ProfileAttributesEntry* entry :
       manager->GetProfileAttributesStorage().GetAllProfilesAttributes()) {
    if (!entry || !IsDurableRegularEntry(*manager, *entry) ||
        TokenForPath(user_data_dir, entry->GetPath()) != opaque_token) {
      continue;
    }
    if (!matched_path.empty()) {
      Complete(request_id, nullptr);
      return;
    }
    matched_path = entry->GetPath();
  }
  if (matched_path.empty()) {
    Complete(request_id, nullptr);
    return;
  }
  if (Profile* loaded = manager->GetProfileByPath(matched_path)) {
    OnProfileLoaded(request_id, matched_path, loaded);
    return;
  }
  if (!manager->LoadProfileByPath(
          matched_path, /*incognito=*/false,
          base::BindOnce(&OnProfileLoaded, request_id, matched_path))) {
    Complete(request_id, nullptr);
  }
}

DEFINE_JNI(TaffyRegularProfileLoader)

}  // namespace taffy

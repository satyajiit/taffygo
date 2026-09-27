// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <jni.h>

#include <algorithm>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "base/android/jni_string.h"
#include "base/containers/flat_map.h"
#include "base/functional/bind.h"
#include "base/no_destructor.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/nuke_profile_directory_utils.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_attributes_entry.h"
#include "chrome/browser/profiles/profile_attributes_storage.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/profiles/profile_metrics.h"
#include "content/public/browser/browser_thread.h"
#include "crypto/hash.h"
#include "taffy/browser/android/browser_profiles_jni_headers/TaffyBrowserProfilesBridge_jni.h"
#include "taffy/browser/backup_restore_profile_registry.h"

namespace taffy {
namespace {

// Native half of TaffyBrowserProfilesBridge's closed, non-user-facing results.
enum class ProfileOperationError : jint {
  kNone = 0,
  kUnavailable = 1,
  kPrivateProfile = 2,
  kNotActive = 3,
  kInvalidName = 4,
  kLimitReached = 5,
  kDuplicateName = 6,
  kNotFound = 7,
  kActiveProfile = 8,
  kLastProfile = 9,
  kBusy = 10,
  kOperationFailed = 11,
  kProfileInUse = 12,
};

constexpr size_t kMaximumRegularProfiles = 8;
constexpr size_t kMaximumProfileNameLength = 40;

bool g_profile_mutation_in_flight = false;
jlong g_next_window_lease_id = 1;

ProfileManager* Manager() {
  return g_browser_process ? g_browser_process->profile_manager() : nullptr;
}

std::string ProfileId(const base::FilePath& path) {
  return base::HexEncode(crypto::hash::Sha256(path.AsUTF8Unsafe()));
}

bool IsRegularEntry(ProfileManager& manager, ProfileAttributesEntry& entry) {
  const base::FilePath path = entry.GetPath();
  PrefService* local_state =
      g_browser_process ? g_browser_process->local_state() : nullptr;
  return !path.empty() && path != ProfileManager::GetGuestProfilePath() &&
         !entry.IsOmitted() && !entry.IsEphemeral() &&
         !IsProfileDirectoryMarkedForDeletion(path) &&
         manager.IsAllowedProfilePath(path) &&
         BackupRestoreQuarantineForProfilePath(local_state, path) ==
             BackupRestoreProfileQuarantineStatus::kNotQuarantined;
}

base::flat_map<jlong, base::FilePath>& WindowLeases() {
  static base::NoDestructor<base::flat_map<jlong, base::FilePath>> leases;
  return *leases;
}

base::FilePath RegularProfilePath(Profile* window_profile) {
  ProfileManager* manager = Manager();
  Profile* regular =
      window_profile ? window_profile->GetOriginalProfile() : nullptr;
  if (!manager || !regular || regular->IsOffTheRecord() ||
      !manager->IsValidProfile(regular)) {
    return {};
  }
  ProfileAttributesEntry* entry =
      manager->GetProfileAttributesStorage().GetProfileAttributesWithPath(
          regular->GetPath());
  return entry && IsRegularEntry(*manager, *entry) ? regular->GetPath()
                                                   : base::FilePath();
}

std::vector<ProfileAttributesEntry*> RegularEntries(ProfileManager& manager) {
  std::vector<ProfileAttributesEntry*> entries;
  for (ProfileAttributesEntry* entry :
       manager.GetProfileAttributesStorage().GetAllProfilesAttributes()) {
    if (entry && IsRegularEntry(manager, *entry)) {
      entries.push_back(entry);
    }
  }
  std::ranges::sort(entries, [](const ProfileAttributesEntry* left,
                                const ProfileAttributesEntry* right) {
    const std::u16string left_name = left->GetLocalProfileName();
    const std::u16string right_name = right->GetLocalProfileName();
    return left_name == right_name ? left->GetPath() < right->GetPath()
                                   : left_name < right_name;
  });
  return entries;
}

ProfileAttributesEntry* FindEntry(ProfileManager& manager,
                                  const std::string& profile_id) {
  if (profile_id.empty() || profile_id.size() != 64) {
    return nullptr;
  }
  for (ProfileAttributesEntry* entry : RegularEntries(manager)) {
    if (ProfileId(entry->GetPath()) == profile_id) {
      return entry;
    }
  }
  return nullptr;
}

ProfileOperationError ValidateCaller(Profile* current_profile,
                                     ProfileManager** manager_out) {
  if (!current_profile || current_profile->IsOffTheRecord()) {
    return ProfileOperationError::kPrivateProfile;
  }
  ProfileManager* manager = Manager();
  if (!manager || !manager->IsValidProfile(current_profile)) {
    return ProfileOperationError::kUnavailable;
  }
  PrefService* local_state =
      g_browser_process ? g_browser_process->local_state() : nullptr;
  const BackupRestoreProfileQuarantineStatus quarantine =
      BackupRestoreQuarantineForProfilePath(local_state,
                                            current_profile->GetPath());
  if (quarantine != BackupRestoreProfileQuarantineStatus::kNotQuarantined) {
    return quarantine == BackupRestoreProfileQuarantineStatus::kQuarantined
               ? ProfileOperationError::kNotActive
               : ProfileOperationError::kUnavailable;
  }
  if (manager->GetLastUsedProfileDir() != current_profile->GetPath() ||
      IsProfileDirectoryMarkedForDeletion(current_profile->GetPath())) {
    return ProfileOperationError::kNotActive;
  }
  *manager_out = manager;
  return ProfileOperationError::kNone;
}

void CompleteOperation(jlong request_id,
                       Profile* profile,
                       ProfileOperationError error) {
  JNIEnv* env = jni_zero::AttachCurrentThread();
  Java_TaffyBrowserProfilesBridge_onOperationCompleted(
      env, request_id,
      profile ? profile->GetJavaObject()
              : jni_zero::ScopedJavaLocalRef<jobject>(),
      static_cast<jint>(error));
}

void CompleteMutation(jlong request_id,
                      Profile* profile,
                      ProfileOperationError error) {
  g_profile_mutation_in_flight = false;
  CompleteOperation(request_id, profile, error);
}

bool BeginMutation(Profile* current_profile,
                   jlong request_id,
                   ProfileManager** manager_out) {
  if (request_id <= 0) {
    CompleteOperation(request_id, nullptr,
                      ProfileOperationError::kOperationFailed);
    return false;
  }
  ProfileOperationError caller_error =
      ValidateCaller(current_profile, manager_out);
  if (caller_error != ProfileOperationError::kNone) {
    CompleteOperation(request_id, nullptr, caller_error);
    return false;
  }
  if (g_profile_mutation_in_flight) {
    CompleteOperation(request_id, nullptr, ProfileOperationError::kBusy);
    return false;
  }
  BackupRestoreProfileReservationList restore_reservations =
      ReadBackupRestoreProfileReservations(
          g_browser_process ? g_browser_process->local_state() : nullptr);
  if (!restore_reservations.has_value() || !restore_reservations->empty()) {
    CompleteOperation(request_id, nullptr,
                      restore_reservations.has_value()
                          ? ProfileOperationError::kBusy
                          : ProfileOperationError::kUnavailable);
    return false;
  }
  g_profile_mutation_in_flight = true;
  return true;
}

std::u16string ValidatedName(JNIEnv* env,
                             const jni_zero::JavaRef<jstring>& java_name) {
  if (!java_name) {
    return {};
  }
  std::u16string name = base::android::ConvertJavaStringToUTF16(env, java_name);
  base::TrimWhitespace(name, base::TRIM_ALL, &name);
  if (name.empty() || name.size() > kMaximumProfileNameLength ||
      std::ranges::any_of(name, [](char16_t character) {
        return character < 0x20 || character == 0x7f;
      })) {
    return {};
  }
  return name;
}

void OnProfileCreated(jlong request_id,
                      std::u16string display_name,
                      Profile* profile) {
  ProfileManager* manager = Manager();
  if (!profile || !manager || !manager->IsValidProfile(profile)) {
    CompleteMutation(request_id, nullptr,
                     ProfileOperationError::kOperationFailed);
    return;
  }
  ProfileAttributesEntry* entry =
      manager->GetProfileAttributesStorage().GetProfileAttributesWithPath(
          profile->GetPath());
  if (!entry || !IsRegularEntry(*manager, *entry)) {
    CompleteMutation(request_id, nullptr,
                     ProfileOperationError::kOperationFailed);
    return;
  }
  entry->SetLocalProfileName(display_name, /*is_default_name=*/false);
  manager->SetProfileAsLastUsed(profile);
  CompleteMutation(request_id, profile, ProfileOperationError::kNone);
}

void OnProfileActivated(jlong request_id, Profile* profile) {
  ProfileManager* manager = Manager();
  if (!profile || !manager || !manager->IsValidProfile(profile) ||
      profile->IsOffTheRecord() ||
      IsProfileDirectoryMarkedForDeletion(profile->GetPath())) {
    CompleteMutation(request_id, nullptr,
                     ProfileOperationError::kOperationFailed);
    return;
  }
  manager->SetProfileAsLastUsed(profile);
  CompleteMutation(request_id, profile, ProfileOperationError::kNone);
}

void OnProfileDeleted(jlong request_id) {
  CompleteMutation(request_id, nullptr, ProfileOperationError::kNone);
}

}  // namespace

static std::vector<std::string> JNI_TaffyBrowserProfilesBridge_ListProfiles(
    JNIEnv* env,
    Profile* current_profile) {
  ProfileManager* manager = nullptr;
  if (ValidateCaller(current_profile, &manager) !=
      ProfileOperationError::kNone) {
    return {};
  }

  std::vector<std::string> fields;
  std::vector<ProfileAttributesEntry*> entries = RegularEntries(*manager);
  // Four fields per profile: id, name, "1" when it is the current profile, and
  // "1" when the name is one Chromium chose rather than a person. The first
  // profile carries Chromium's "Your Chromium"; product UI shows its own name
  // for a default-named profile instead (decision 0255).
  fields.reserve(entries.size() * 4);
  for (ProfileAttributesEntry* entry : entries) {
    std::u16string display_name = entry->GetLocalProfileName();
    const bool uses_default_name =
        display_name.empty() || entry->IsUsingDefaultName();
    if (display_name.empty()) {
      display_name = u"Profile";
    }
    fields.push_back(ProfileId(entry->GetPath()));
    fields.push_back(base::UTF16ToUTF8(display_name));
    fields.emplace_back(entry->GetPath() == current_profile->GetPath() ? "1"
                                                                       : "0");
    fields.emplace_back(uses_default_name ? "1" : "0");
  }
  return fields;
}

static jlong JNI_TaffyBrowserProfilesBridge_RegisterWindowLease(
    JNIEnv* env,
    Profile* window_profile) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  auto& leases = WindowLeases();
  const base::FilePath path = RegularProfilePath(window_profile);
  if (path.empty() || g_next_window_lease_id <= 0) {
    return 0;
  }
  const jlong lease_id = g_next_window_lease_id;
  g_next_window_lease_id =
      lease_id == std::numeric_limits<jlong>::max() ? 0 : lease_id + 1;
  leases.emplace(lease_id, path);
  return lease_id;
}

static void JNI_TaffyBrowserProfilesBridge_UnregisterWindowLease(
    JNIEnv* env,
    jlong lease_id) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  WindowLeases().erase(lease_id);
}

static void JNI_TaffyBrowserProfilesBridge_CreateProfile(
    JNIEnv* env,
    Profile* current_profile,
    const jni_zero::JavaRef<jstring>& java_display_name,
    jlong request_id) {
  ProfileManager* manager = nullptr;
  if (!BeginMutation(current_profile, request_id, &manager)) {
    return;
  }
  const std::u16string display_name = ValidatedName(env, java_display_name);
  if (display_name.empty()) {
    CompleteMutation(request_id, nullptr, ProfileOperationError::kInvalidName);
    return;
  }
  std::vector<ProfileAttributesEntry*> entries = RegularEntries(*manager);
  if (entries.size() >= kMaximumRegularProfiles) {
    CompleteMutation(request_id, nullptr, ProfileOperationError::kLimitReached);
    return;
  }
  if (std::ranges::any_of(entries, [&display_name](const auto* entry) {
        return entry->GetLocalProfileName() == display_name;
      })) {
    CompleteMutation(request_id, nullptr,
                     ProfileOperationError::kDuplicateName);
    return;
  }

  const base::FilePath path = manager->GenerateNextProfileDirectoryPath();
  manager->CreateProfileAsync(
      path, base::BindOnce(&OnProfileCreated, request_id, display_name));
}

static void JNI_TaffyBrowserProfilesBridge_ActivateProfile(
    JNIEnv* env,
    Profile* current_profile,
    const jni_zero::JavaRef<jstring>& java_profile_id,
    jlong request_id) {
  ProfileManager* manager = nullptr;
  if (!BeginMutation(current_profile, request_id, &manager)) {
    return;
  }
  if (!java_profile_id) {
    CompleteMutation(request_id, nullptr, ProfileOperationError::kNotFound);
    return;
  }
  const std::string profile_id =
      base::android::ConvertJavaStringToUTF8(env, java_profile_id);
  ProfileAttributesEntry* entry = FindEntry(*manager, profile_id);
  if (!entry) {
    CompleteMutation(request_id, nullptr, ProfileOperationError::kNotFound);
    return;
  }
  if (entry->GetPath() == current_profile->GetPath()) {
    CompleteMutation(request_id, current_profile, ProfileOperationError::kNone);
    return;
  }
  if (Profile* loaded = manager->GetProfileByPath(entry->GetPath())) {
    OnProfileActivated(request_id, loaded);
    return;
  }
  if (!manager->LoadProfileByPath(
          entry->GetPath(), /*incognito=*/false,
          base::BindOnce(&OnProfileActivated, request_id))) {
    CompleteMutation(request_id, nullptr,
                     ProfileOperationError::kOperationFailed);
  }
}

static void JNI_TaffyBrowserProfilesBridge_DeleteProfile(
    JNIEnv* env,
    Profile* current_profile,
    const jni_zero::JavaRef<jstring>& java_profile_id,
    jlong request_id) {
  ProfileManager* manager = nullptr;
  if (!BeginMutation(current_profile, request_id, &manager)) {
    return;
  }
  if (!java_profile_id) {
    CompleteMutation(request_id, nullptr, ProfileOperationError::kNotFound);
    return;
  }
  const std::string profile_id =
      base::android::ConvertJavaStringToUTF8(env, java_profile_id);
  std::vector<ProfileAttributesEntry*> entries = RegularEntries(*manager);
  ProfileAttributesEntry* entry = FindEntry(*manager, profile_id);
  if (!entry) {
    CompleteMutation(request_id, nullptr, ProfileOperationError::kNotFound);
    return;
  }
  if (std::ranges::any_of(WindowLeases(), [&entry](const auto& lease) {
        return lease.second == entry->GetPath();
      })) {
    CompleteMutation(request_id, nullptr, ProfileOperationError::kProfileInUse);
    return;
  }
  if (entry->GetPath() == current_profile->GetPath()) {
    CompleteMutation(request_id, nullptr,
                     ProfileOperationError::kActiveProfile);
    return;
  }
  if (entries.size() <= 1) {
    CompleteMutation(request_id, nullptr, ProfileOperationError::kLastProfile);
    return;
  }
  if (!manager->DeleteProfileOnAndroid(
          entry->GetPath(), base::BindOnce(&OnProfileDeleted, request_id),
          ProfileMetrics::DELETE_PROFILE_SETTINGS)) {
    CompleteMutation(request_id, nullptr,
                     ProfileOperationError::kOperationFailed);
  }
}

DEFINE_JNI(TaffyBrowserProfilesBridge)

}  // namespace taffy

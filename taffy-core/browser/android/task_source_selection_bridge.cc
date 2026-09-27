// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <jni.h>

#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "base/android/jni_string.h"
#include "base/android/scoped_java_ref.h"
#include "base/containers/flat_map.h"
#include "base/memory/raw_ptr.h"
#include "chrome/browser/android/tab_android.h"
#include "chrome/browser/profiles/profile.h"
#include "content/public/browser/web_contents.h"
#include "taffy/browser/android/pdf_intelligence_bridge.h"
#include "taffy/browser/android/task_navigation_authority_chrome.h"
#include "taffy/browser/android/task_source_selection_jni_headers/TaffyTaskSourceSelectionBridge_jni.h"
#include "taffy/browser/core_service_manager.h"
#include "taffy/browser/core_service_manager_factory.h"
#include "taffy/browser/task_source_selection_registry.h"

namespace taffy {
namespace {

CoreServiceManager* Manager(Profile* profile) {
  return profile ? CoreServiceManagerFactory::GetForProfile(profile) : nullptr;
}

CoreServiceManager* ExistingManager(Profile* profile) {
  return profile ? CoreServiceManagerFactory::GetForProfileIfExists(profile)
                 : nullptr;
}

bool IsExactProfileTab(Profile* profile, TabAndroid* tab) {
  return profile && tab && tab->profile() == profile && tab->web_contents() &&
         tab->web_contents()->GetBrowserContext() == profile;
}

bool IsSameProfileTab(Profile* profile, TabAndroid* tab) {
  return profile && tab && tab->profile() == profile;
}

class TaskBrowserActionBridge final : public TaskBrowserActionPlatform {
 public:
  TaskBrowserActionBridge(const jni_zero::JavaRef<jobject>& java_bridge,
                          uint64_t window_token,
                          Profile* profile)
      : java_bridge_(java_bridge),
        window_token_(window_token),
        profile_(profile) {}

  TaskBrowserActionBridge(const TaskBrowserActionBridge&) = delete;
  TaskBrowserActionBridge& operator=(const TaskBrowserActionBridge&) = delete;
  ~TaskBrowserActionBridge() override {
    auto callbacks = std::move(discovery_callbacks_);
    for (auto& entry : callbacks) {
      std::move(entry.second).Run(nullptr);
    }
  }

  uint64_t window_token() const { return window_token_; }

  std::optional<std::string> ResolveSearchAddress(
      const std::string& query) override {
    JNIEnv* env = jni_zero::AttachCurrentThread();
    auto result = Java_TaffyTaskSourceSelectionBridge_resolveSearchAddress(
        env, java_bridge_, base::android::ConvertUTF8ToJavaString(env, query));
    if (!result) {
      return std::nullopt;
    }
    const std::string address =
        base::android::ConvertJavaStringToUTF8(env, result);
    return address.empty() ? std::nullopt : std::optional<std::string>(address);
  }

  content::WebContents* OpenTaskTab(
      const std::string& task_id,
      const std::string& action_id,
      const std::string& destination_address) override {
    JNIEnv* env = jni_zero::AttachCurrentThread();
    auto java_tab = Java_TaffyTaskSourceSelectionBridge_openTaskTab(
        env, java_bridge_, base::android::ConvertUTF8ToJavaString(env, task_id),
        base::android::ConvertUTF8ToJavaString(env, action_id),
        base::android::ConvertUTF8ToJavaString(env, destination_address));
    TabAndroid* tab = TabAndroid::GetNativeTab(env, java_tab);
    return tab ? tab->web_contents() : nullptr;
  }

  void OpenTaskDiscoveryTab(const std::string& task_id,
                            const std::string& effect_id,
                            TaskDiscoveryTabCallback callback) override {
    if (!callback || next_discovery_request_id_ <= 0 ||
        next_discovery_request_id_ == std::numeric_limits<jlong>::max() ||
        discovery_callbacks_.size() >=
            core_service::mojom::kMaxInFlightPerProfile) {
      if (callback) {
        std::move(callback).Run(nullptr);
      }
      return;
    }
    const jlong request_id = next_discovery_request_id_++;
    if (discovery_callbacks_.contains(request_id)) {
      std::move(callback).Run(nullptr);
      return;
    }
    discovery_callbacks_.emplace(request_id, std::move(callback));
    JNIEnv* env = jni_zero::AttachCurrentThread();
    Java_TaffyTaskSourceSelectionBridge_openTaskDiscoveryTab(
        env, java_bridge_, base::android::ConvertUTF8ToJavaString(env, task_id),
        base::android::ConvertUTF8ToJavaString(env, effect_id), request_id);
  }

  void CompleteTaskDiscoveryTab(Profile* profile,
                                jlong request_id,
                                TabAndroid* tab) {
    auto request = discovery_callbacks_.find(request_id);
    if (request == discovery_callbacks_.end()) {
      return;
    }
    TaskDiscoveryTabCallback callback = std::move(request->second);
    discovery_callbacks_.erase(request);
    content::WebContents* web_contents =
        profile == profile_ && IsExactProfileTab(profile, tab)
            ? tab->web_contents()
            : nullptr;
    std::move(callback).Run(web_contents);
  }

  bool StartSearch(content::WebContents* web_contents,
                   const std::string& query,
                   const std::string& destination_address) override {
    TabAndroid* tab = TabAndroid::FromWebContents(web_contents);
    if (!tab) {
      return false;
    }
    JNIEnv* env = jni_zero::AttachCurrentThread();
    return Java_TaffyTaskSourceSelectionBridge_startSearch(
        env, java_bridge_, tab->GetJavaObject(),
        base::android::ConvertUTF8ToJavaString(env, query),
        base::android::ConvertUTF8ToJavaString(env, destination_address));
  }

  bool ActivateTaskTab(content::WebContents* web_contents) override {
    TabAndroid* tab = TabAndroid::FromWebContents(web_contents);
    return tab && Java_TaffyTaskSourceSelectionBridge_activateTaskTab(
                      jni_zero::AttachCurrentThread(), java_bridge_,
                      tab->GetJavaObject());
  }

  bool CloseTaskTab(content::WebContents* web_contents) override {
    TabAndroid* tab = TabAndroid::FromWebContents(web_contents);
    return tab && Java_TaffyTaskSourceSelectionBridge_closeTaskTab(
                      jni_zero::AttachCurrentThread(), java_bridge_,
                      tab->GetJavaObject());
  }

 private:
  const jni_zero::ScopedJavaGlobalRef<jobject> java_bridge_;
  const uint64_t window_token_;
  const raw_ptr<Profile> profile_;
  base::flat_map<jlong, TaskDiscoveryTabCallback> discovery_callbacks_;
  jlong next_discovery_request_id_ = 1;
};

TaskBrowserActionBridge* BrowserActionsFromPtr(jlong bridge_ptr) {
  return reinterpret_cast<TaskBrowserActionBridge*>(bridge_ptr);
}

}  // namespace

static jlong JNI_TaffyTaskSourceSelectionBridge_RegisterWindow(
    JNIEnv* env,
    Profile* profile) {
  CoreServiceManager* manager = Manager(profile);
  if (manager) {
    InstallAndroidPdfIntelligenceBridge();
    InstallChromeTaskNavigationAuthorityPlatform();
  }
  return manager ? static_cast<jlong>(manager->RegisterTaskSourceWindow()) : 0;
}

static void JNI_TaffyTaskSourceSelectionBridge_UnregisterWindow(
    JNIEnv* env,
    Profile* profile,
    jlong window_token) {
  if (CoreServiceManager* manager = ExistingManager(profile)) {
    manager->UnregisterTaskSourceWindow(static_cast<uint64_t>(window_token));
  }
}

static jboolean JNI_TaffyTaskSourceSelectionBridge_RegisterTab(
    JNIEnv* env,
    Profile* profile,
    jlong window_token,
    TabAndroid* tab,
    jboolean session_restored) {
  CoreServiceManager* manager = ExistingManager(profile);
  return manager && IsExactProfileTab(profile, tab) &&
         manager->RegisterTaskSourceTab(static_cast<uint64_t>(window_token),
                                        tab->GetAndroidId(),
                                        tab->web_contents(), session_restored);
}

static void JNI_TaffyTaskSourceSelectionBridge_UnregisterTab(JNIEnv* env,
                                                             Profile* profile,
                                                             jlong window_token,
                                                             TabAndroid* tab) {
  CoreServiceManager* manager = ExistingManager(profile);
  if (manager && IsSameProfileTab(profile, tab)) {
    manager->UnregisterTaskSourceTab(static_cast<uint64_t>(window_token),
                                     tab->GetAndroidId());
  }
}

static jboolean JNI_TaffyTaskSourceSelectionBridge_Select(JNIEnv* env,
                                                          Profile* profile,
                                                          jlong window_token,
                                                          TabAndroid* tab) {
  CoreServiceManager* manager = ExistingManager(profile);
  return manager && IsExactProfileTab(profile, tab) &&
         manager->SelectTaskSourceTab(static_cast<uint64_t>(window_token),
                                      tab->GetAndroidId(), tab->web_contents());
}

static void JNI_TaffyTaskSourceSelectionBridge_ClearSelection(
    JNIEnv* env,
    Profile* profile,
    jlong window_token) {
  if (CoreServiceManager* manager = ExistingManager(profile)) {
    manager->ClearTaskSourceSelection(static_cast<uint64_t>(window_token));
  }
}

static jboolean JNI_TaffyTaskSourceSelectionBridge_Activate(
    JNIEnv* env,
    Profile* profile,
    jlong window_token) {
  CoreServiceManager* manager = ExistingManager(profile);
  return manager &&
         manager->ActivateTaskSourceWindow(static_cast<uint64_t>(window_token));
}

static void JNI_TaffyTaskSourceSelectionBridge_Deactivate(JNIEnv* env,
                                                          Profile* profile,
                                                          jlong window_token) {
  if (CoreServiceManager* manager = ExistingManager(profile)) {
    manager->DeactivateTaskSourceWindow(static_cast<uint64_t>(window_token));
  }
}

static jlong JNI_TaffyTaskSourceSelectionBridge_InitBrowserActions(
    JNIEnv* env,
    const jni_zero::JavaRef<jobject>& caller,
    Profile* profile,
    jlong window_token) {
  CoreServiceManager* manager = ExistingManager(profile);
  if (!manager || window_token <= 0) {
    return 0;
  }
  auto bridge = std::make_unique<TaskBrowserActionBridge>(
      caller, static_cast<uint64_t>(window_token), profile);
  if (!manager->BindTaskBrowserActionPlatform(bridge->window_token(),
                                              bridge.get())) {
    return 0;
  }
  return reinterpret_cast<jlong>(bridge.release());
}

static void JNI_TaffyTaskSourceSelectionBridge_DestroyBrowserActions(
    JNIEnv* env,
    Profile* profile,
    jlong bridge_ptr) {
  std::unique_ptr<TaskBrowserActionBridge> bridge(
      BrowserActionsFromPtr(bridge_ptr));
  if (!bridge) {
    return;
  }
  if (CoreServiceManager* manager = ExistingManager(profile)) {
    manager->UnbindTaskBrowserActionPlatform(bridge->window_token(),
                                             bridge.get());
  }
}

static void JNI_TaffyTaskSourceSelectionBridge_CompleteTaskDiscoveryTab(
    JNIEnv* env,
    Profile* profile,
    jlong bridge_ptr,
    jlong request_id,
    TabAndroid* tab) {
  TaskBrowserActionBridge* bridge = BrowserActionsFromPtr(bridge_ptr);
  if (bridge) {
    bridge->CompleteTaskDiscoveryTab(profile, request_id, tab);
  }
}

static jboolean JNI_TaffyTaskSourceSelectionBridge_ClaimAssistantCreatedTaskTab(
    JNIEnv* env,
    Profile* profile,
    jlong window_token,
    const std::string& task_id,
    const std::string& action_id,
    TabAndroid* tab) {
  CoreServiceManager* manager = ExistingManager(profile);
  return manager && IsExactProfileTab(profile, tab) &&
         manager->ClaimAssistantCreatedTaskTab(
             static_cast<uint64_t>(window_token), task_id, action_id,
             tab->web_contents());
}

static jboolean JNI_TaffyTaskSourceSelectionBridge_IsAssistantCreatedTaskTab(
    JNIEnv* env,
    Profile* profile,
    TabAndroid* tab) {
  return IsExactProfileTab(profile, tab) &&
         TaskSourceSelectionRegistry::BrowserOwnedProvenance(
             tab->web_contents()) == TaskSourceTabProvenance::kAssistantCreated;
}

static std::string JNI_TaffyTaskSourceSelectionBridge_GetCreatingTaskId(
    JNIEnv* env,
    Profile* profile,
    TabAndroid* tab) {
  return IsExactProfileTab(profile, tab)
             ? TaskSourceSelectionRegistry::BrowserOwnedTaskId(
                   tab->web_contents())
             : std::string();
}

static jboolean JNI_TaffyTaskSourceSelectionBridge_IsAcceptedTaskSourceTab(
    JNIEnv* env,
    Profile* profile,
    const std::string& task_id,
    TabAndroid* tab) {
  CoreServiceManager* manager = ExistingManager(profile);
  return manager && IsExactProfileTab(profile, tab) &&
         manager->IsTaskSourceTabForDisplay(task_id, tab->web_contents());
}

DEFINE_JNI(TaffyTaskSourceSelectionBridge)

}  // namespace taffy

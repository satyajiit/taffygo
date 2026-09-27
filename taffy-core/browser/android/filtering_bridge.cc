// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <jni.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "base/android/jni_string.h"
#include "base/callback_list.h"
#include "base/containers/flat_set.h"
#include "base/functional/bind.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "chrome/browser/android/tab_android.h"
#include "chrome/browser/profiles/profile.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "taffy/browser/android/filtering_jni_headers/TaffyFilteringBridge_jni.h"
#include "taffy/browser/core_service_manager.h"
#include "taffy/browser/core_service_manager_factory.h"
#include "taffy/components/filtering/browser/filtering_ruleset_service.h"
#include "taffy/components/filtering/browser/filtering_tab_counters.h"
#include "url/gurl.h"

namespace taffy {
namespace {

// One profile's Java-held filtering seam. Owned by the Java bridge through
// its native pointer, destroyed by close() on the UI thread — the same thread
// every service callback runs on. The service is held weakly because the
// profile owns it and a Java object's lifetime is the garbage collector's
// business, not the profile's. A subscription belongs to this exact window, so
// closing one bridge cannot withdraw another window's projection.
class FilteringBridge {
 public:
  FilteringBridge(const jni_zero::JavaRef<jobject>& java_bridge,
                  base::WeakPtr<filtering::FilteringRulesetService> service)
      : java_bridge_(java_bridge), service_(std::move(service)) {
    if (service_) {
      // Posture edits, ruleset compiles, and every tab's coalesced count
      // publication all arrive here. The profile signal fans out to every
      // live product window; the returned capability owns only this one.
      changed_subscription_ = service_->AddChangedCallback(base::BindRepeating(
          &FilteringBridge::OnChanged, weak_factory_.GetWeakPtr()));
    }
  }

  ~FilteringBridge() = default;

  FilteringBridge(const FilteringBridge&) = delete;
  FilteringBridge& operator=(const FilteringBridge&) = delete;

  filtering::FilteringRulesetService* service() { return service_.get(); }

 private:
  void OnChanged() {
    // Java may synchronously close this bridge. Do not access members after
    // crossing JNI; the weak callback and subscription make that close safe.
    Java_TaffyFilteringBridge_onChanged(jni_zero::AttachCurrentThread(),
                                        java_bridge_);
  }

  const jni_zero::ScopedJavaGlobalRef<jobject> java_bridge_;
  const base::WeakPtr<filtering::FilteringRulesetService> service_;
  base::CallbackListSubscription changed_subscription_;
  // Last so destruction invalidates a callback before its subscription and
  // Java reference are released.
  base::WeakPtrFactory<FilteringBridge> weak_factory_{this};
};

FilteringBridge* FromPtr(jlong bridge_ptr) {
  return reinterpret_cast<FilteringBridge*>(bridge_ptr);
}

content::WebContents* ContentsOf(TabAndroid* tab) {
  return tab ? tab->web_contents() : nullptr;
}

}  // namespace

static jlong JNI_TaffyFilteringBridge_Init(
    JNIEnv* env,
    const jni_zero::JavaRef<jobject>& caller,
    Profile* profile) {
  if (!profile) {
    return 0;
  }
  CoreServiceManager* manager =
      CoreServiceManagerFactory::GetForProfile(profile);
  if (!manager) {
    return 0;
  }
  return reinterpret_cast<jlong>(new FilteringBridge(
      caller, manager->filtering_service()->GetWeakPtr()));
}

static void JNI_TaffyFilteringBridge_Destroy(JNIEnv* env, jlong bridge_ptr) {
  delete FromPtr(bridge_ptr);
}

static jboolean JNI_TaffyFilteringBridge_IsActiveFor(JNIEnv* env,
                                                     jlong bridge_ptr,
                                                     TabAndroid* tab) {
  filtering::FilteringRulesetService* service = FromPtr(bridge_ptr)->service();
  content::WebContents* contents = ContentsOf(tab);
  if (!service || !contents) {
    return false;
  }
  scoped_refptr<const filtering::SharedRuleset> ruleset = service->ruleset();
  if (!ruleset) {
    return false;
  }
  // The same questions the request path asks, in the same order — see
  // filtering_throttle_factory.cc. A page this answers true for is a page
  // whose next subresource request would be judged.
  const GURL url = contents->GetLastCommittedURL();
  if (!url.SchemeIsHTTPOrHTTPS()) {
    return false;
  }
  if (!service->posture().ActiveForHost(url.host())) {
    return false;
  }
  return !ruleset->matcher().IsDocumentAllowlisted(
      url, contents->GetPrimaryMainFrame()->GetLastCommittedOrigin());
}

// Whether a person has allowed this tab's committed site. The host is read
// and answered inside C++ on purpose: a private tab's host must not cross this
// seam, so what travels is one boolean (decision 0128).
static jboolean JNI_TaffyFilteringBridge_IsExceptedFor(JNIEnv* env,
                                                       jlong bridge_ptr,
                                                       TabAndroid* tab) {
  filtering::FilteringRulesetService* service = FromPtr(bridge_ptr)->service();
  content::WebContents* contents = ContentsOf(tab);
  if (!service || !contents) {
    return false;
  }
  const GURL url = contents->GetLastCommittedURL();
  if (!url.SchemeIsHTTPOrHTTPS()) {
    return false;
  }
  return service->posture().ExceptedForHost(url.host());
}

static jint JNI_TaffyFilteringBridge_BlockedCountFor(JNIEnv* env,
                                                     jlong bridge_ptr,
                                                     TabAndroid* tab) {
  content::WebContents* contents = ContentsOf(tab);
  if (!contents) {
    return 0;
  }
  filtering::FilteringTabCounters* counters =
      filtering::FilteringTabCounters::FromWebContents(contents);
  // No counter means no filtered request has reached this tab yet, and the
  // honest count of a page nothing was blocked on is zero.
  return counters ? static_cast<jint>(counters->blocked_count()) : 0;
}

static void JNI_TaffyFilteringBridge_FlushCountFor(JNIEnv* env,
                                                   jlong bridge_ptr,
                                                   TabAndroid* tab) {
  content::WebContents* contents = ContentsOf(tab);
  if (!contents) {
    return;
  }
  if (filtering::FilteringTabCounters* counters =
          filtering::FilteringTabCounters::FromWebContents(contents)) {
    counters->FlushNow();
  }
}

static jboolean JNI_TaffyFilteringBridge_IsEnabled(JNIEnv* env,
                                                   jlong bridge_ptr) {
  filtering::FilteringRulesetService* service = FromPtr(bridge_ptr)->service();
  return service && service->posture().enabled();
}

static void JNI_TaffyFilteringBridge_SetEnabled(JNIEnv* env,
                                                jlong bridge_ptr,
                                                jboolean enabled) {
  if (filtering::FilteringRulesetService* service =
          FromPtr(bridge_ptr)->service()) {
    service->SetFilteringEnabled(enabled);
  }
}

static jboolean JNI_TaffyFilteringBridge_SetSiteException(
    JNIEnv* env,
    jlong bridge_ptr,
    const std::string& host,
    jboolean allow) {
  filtering::FilteringRulesetService* service = FromPtr(bridge_ptr)->service();
  return service && service->SetSiteException(host, allow);
}

static std::vector<std::string> JNI_TaffyFilteringBridge_SiteExceptions(
    JNIEnv* env,
    jlong bridge_ptr) {
  filtering::FilteringRulesetService* service = FromPtr(bridge_ptr)->service();
  if (!service) {
    return {};
  }
  const base::flat_set<std::string>& hosts =
      service->posture().exception_hosts();
  return std::vector<std::string>(hosts.begin(), hosts.end());
}

static jlong JNI_TaffyFilteringBridge_PostureRevision(JNIEnv* env,
                                                       jlong bridge_ptr) {
  filtering::FilteringRulesetService* service = FromPtr(bridge_ptr)->service();
  return service ? static_cast<jlong>(service->posture_revision()) : 0;
}

static jlong JNI_TaffyFilteringBridge_BlockedTotal(JNIEnv* env,
                                                   jlong bridge_ptr) {
  filtering::FilteringRulesetService* service = FromPtr(bridge_ptr)->service();
  return service ? static_cast<jlong>(service->blocked_total()) : 0;
}

static jlong JNI_TaffyFilteringBridge_BlockedThisWeek(JNIEnv* env,
                                                      jlong bridge_ptr) {
  filtering::FilteringRulesetService* service = FromPtr(bridge_ptr)->service();
  return service ? static_cast<jlong>(service->blocked_this_week()) : 0;
}

static jint JNI_TaffyFilteringBridge_MinimumSitesThisWeek(JNIEnv* env,
                                                          jlong bridge_ptr) {
  filtering::FilteringRulesetService* service = FromPtr(bridge_ptr)->service();
  return service ? static_cast<jint>(service->minimum_sites_this_week()) : 0;
}

static jboolean JNI_TaffyFilteringBridge_HasWeekWindow(JNIEnv* env,
                                                       jlong bridge_ptr) {
  filtering::FilteringRulesetService* service = FromPtr(bridge_ptr)->service();
  return service && service->has_week_window();
}

DEFINE_JNI(TaffyFilteringBridge)

}  // namespace taffy

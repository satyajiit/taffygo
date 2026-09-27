// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/feature_posture.h"

#include <functional>
#include <vector>

#include "base/feature_list.h"
#include "base/values.h"
#include "build/build_config.h"
#include "components/autofill/core/common/autofill_debug_features.h"
#include "components/feed/core/shared_prefs/pref_names.h"
#include "components/feed/feed_feature_list.h"
#include "components/network_time/network_time_tracker.h"
#include "components/optimization_guide/core/optimization_guide_features.h"
#include "components/password_manager/core/common/password_manager_pref_names.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/signin/public/base/signin_pref_names.h"
#include "components/signin/public/base/signin_switches.h"
#include "components/translate/core/browser/translate_ranker_impl.h"
#include "media/base/media_switches.h"
#include "taffy/browser/profile_preferences.h"

#if BUILDFLAG(IS_ANDROID)
// A dependency-free enum header. //chrome/browser depends on this target
// through patch 0016, so a GN edge back to //chrome/browser/download would be
// a cycle.
#include "chrome/browser/download/download_prompt_status.h"  // nogncheck
#include "chrome/common/pref_names.h"
#endif

namespace taffy {

namespace {

// VERIFIED AT 152.0.7977.42: every feature and pref constant below, each in
// the upstream header its include names. A constant that moves is a compile
// error here — the build stops rather than quietly reviving a service.
//
// The reasons cite decision 0019's inventory. Ordering follows that
// inventory, not importance: every entry is load-bearing.

constexpr auto kDisable =
    base::FeatureList::OverrideState::OVERRIDE_DISABLE_FEATURE;
constexpr auto kEnable =
    base::FeatureList::OverrideState::OVERRIDE_ENABLE_FEATURE;

const FeaturePostureEntry kFeaturePosture[] = {
    {&network_time::kNetworkTimeServiceQuerying, kDisable,
     "Queries a Google clock endpoint in the background, roughly daily "
     "(components/network_time/network_time_tracker.cc). The SSL bad-clock "
     "heuristics fall back to build-time data, accepted by decision 0019."},
    {&feed::kInterestFeedV2, kDisable,
     "The Feed sends queries carrying client data to a Google endpoint that "
     "rejects them for want of an API key (components/feed/core/v2/"
     "feed_network_impl.cc). The pref defaults below hide the surface too."},
    {&autofill::features::debug::kAutofillServerCommunication, kDisable,
     "Crowdsourcing queries and votes to content-autofill.googleapis.com; an "
     "empty API key does not stop the request (components/autofill/core/"
     "browser/crowdsourcing/autofill_crowdsourcing_manager.cc). Local "
     "heuristics remain."},
    {&translate::kTranslateRankerQuery, kDisable,
     "The translate ranker fetches its model from a Google static host in "
     "the background (components/translate/core/browser/"
     "translate_ranker_impl.cc). Translation itself stays available — the "
     "posture is 'kept, disclosed' — the ranker is traffic the user never "
     "sees."},
    {&translate::kTranslateRankerEnforcement, kDisable,
     "The enforcement half of the ranker above; both halves query, so both "
     "are off."},
    {&optimization_guide::features::kOptimizationHints, kDisable,
     "Belt-and-braces: the hints fetch is already inert without an API key "
     "and consent (components/optimization_guide/core/"
     "optimization_guide_permissions_util.cc), and decision 0019 records "
     "inert postures with tripwires rather than trusting them."},
    {&optimization_guide::features::kOptimizationTargetPrediction, kDisable,
     "Belt-and-braces, same grounds as the hints fetch above."},
#if BUILDFLAG(IS_ANDROID)
    // MediaDrm is the Android content-decryption path and this feature is
    // declared only in the Android half of media/base/media_switches.h, so
    // the entry is guarded rather than the include: on a host build there is
    // no such startup traffic to switch off, and naming a constant that does
    // not exist there would make taffy-core/browser unbuildable off Android.
    {&media::kMediaDrmPreprovisioning, kDisable,
     "Pre-provisions MediaDrm origin certificates at startup against the "
     "Widevine provisioning service (found in the first egress capture: "
     "repeated googleapis certificateprovisioning calls on an idle fresh "
     "profile). Disabling defers provisioning to first EME use, so DRM "
     "playback capability is unchanged (OD-021 owns that) and the covert "
     "startup traffic is gone."},
    {&media::kAndroidEnableBackgroundMediaCapturing, kDisable,
     "Not a Google posture: a manifest coupling. Background capture is the "
     "only path that starts MediaCaptureNotificationService in the foreground "
     "with the camera, microphone and media-projection types, and the product "
     "manifest declares none of them (upstream patch 0052, decision 0252). "
     "Upstream enables it on desktop Android builds only; if its default ever "
     "changed, capture would ask Android for a type the app never declared."},
#endif
    {&switches::kAvoidAutoTriggerListAccountsOnStale, kEnable,
     "The one deliberate enable: the flag's ON state is the quiet one, "
     "suppressing automatic GAIA ListAccounts refreshes when the cookie jar "
     "goes stale. The engine has no Google account feature — TaffyGo's own "
     "account plane is app-layer (decision 0019's boundary) — so nothing "
     "consumes the result."},
};

const PrefPostureEntry kPrefDefaults[] = {
    {password_manager::prefs::kPasswordLeakDetectionEnabled, false,
     "The keyless leak-detection service fails server-side while the "
     "encrypted credential payload still leaves the device, so off is "
     "strictly better until OD-080 lands a replacement provider."},
    {prefs::kSigninAllowed, false,
     "The engine's Google sign-in is not a TaffyGo feature: the product's "
     "account protocol is portable Rust, reached through browser-owned "
     "network and secure-store brokers and Android credential/redirect "
     "surfaces. Default-off retires the sign-in promos, the settings entry, "
     "and the services gated on SigninManager::IsSigninAllowed."},
    {feed::prefs::kEnableSnippets, false,
     "The Feed's content half; the feature override above kills the "
     "network path, this hides the surface for a fresh profile."},
    {feed::prefs::kArticlesListVisible, false,
     "The Feed's visibility half, same grounds as kEnableSnippets."},
};

}  // namespace

void ApplyFeaturePosture(base::FeatureList* feature_list) {
  std::vector<base::FeatureList::FeatureOverrideInfo> overrides;
  overrides.reserve(std::size(kFeaturePosture));
  for (const FeaturePostureEntry& entry : kFeaturePosture) {
    overrides.emplace_back(std::cref(*entry.feature), entry.state);
  }
  feature_list->RegisterExtraFeatureOverrides(overrides);
}

void OverrideProfilePrefDefaults(PrefRegistrySimple* registry) {
  // This call is made from RegisterProfilePrefs before the PrefService exists.
  // Register TaffyGo's own profile values at the same reviewed seam, then
  // replace the defaults of the already-registered upstream values below.
  profile_preferences::RegisterProfilePreferences(registry);
  for (const PrefPostureEntry& entry : kPrefDefaults) {
    registry->SetDefaultPrefValue(entry.pref_name,
                                  base::Value(entry.default_value));
  }
#if BUILDFLAG(IS_ANDROID)
  // Not a Google posture: a dialog this shell cannot show. Chromium asks where
  // to save a first download through the activity's own dialog manager, and
  // TaffyBrowserActivity has none, so the dialog was dismissed as if the
  // activity were being destroyed and the download was cancelled without a
  // word. Every file a page offered was lost that way. A download now goes to
  // the download directory without asking, which is the path the errand
  // download tests already set.
  registry->SetDefaultPrefValue(
      prefs::kPromptForDownloadAndroid,
      base::Value(static_cast<int>(DownloadPromptStatus::DONT_SHOW)));
#endif
}

base::span<const FeaturePostureEntry> FeaturePostureForTesting() {
  return kFeaturePosture;
}

base::span<const PrefPostureEntry> PrefPostureForTesting() {
  return kPrefDefaults;
}

}  // namespace taffy

// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_PROFILE_PREFERENCES_H_
#define TAFFY_BROWSER_PROFILE_PREFERENCES_H_

class PrefRegistrySimple;

namespace taffy::profile_preferences {

inline constexpr char kTheme[] = "taffy.ui.theme";
inline constexpr char kAppLanguage[] = "taffy.ui.app_language";
inline constexpr char kRegionCode[] = "taffy.ui.region_code";
inline constexpr char kPseudoLocalization[] = "taffy.ui.pseudo_localization";
inline constexpr char kForceDarkWeb[] = "taffy.ui.force_dark_web";
inline constexpr char kProviderRoute[] = "taffy.model.provider_route";

// One person's standing model choice per provider (decision 0093): a
// dictionary keyed by provider id, each value carrying an optional model id
// and an optional thinking level. The browser owns what persists and the
// isolated core holds only what it needs to decide, so this is read at
// bootstrap and replayed into the core rather than journalled with tasks —
// a preference is not something a task did.
//
// Reading and writing it is `taffy/browser/model/
// provider_model_preference_store.h`; nothing else may name this string.
inline constexpr char kProviderModelPreferences[] =
    "taffy.model.provider_preferences";

// The register of decision 0096: per provider id, the exact endpoint string
// the person typed and saved. A dictionary keyed by provider id whose value is
// that string and nothing else — no parse of it, no origin derived from it,
// no normalization.
//
// It is held here rather than in the isolated core because the core is the
// component a model's output reaches, and a component inside the trust
// boundary that can name an arbitrary address is a request-forgery primitive.
// A model request claiming a person's own address is answered by comparing it
// with what this file holds, byte for byte.
//
// Reading and writing it is `taffy/browser/model/
// custom_provider_endpoint_store.h`; nothing else may name this string.
inline constexpr char kCustomProviderEndpoints[] =
    "taffy.model.custom_provider_endpoints";

inline constexpr char kNotificationTopics[] = "taffy.ui.notification_topics";
inline constexpr char kOnboardingCompleted[] = "taffy.ui.onboarding_completed";
inline constexpr char kDiagnosticsOptIn[] = "taffy.ui.diagnostics_opt_in";

// Whether the composer offers a suggestion while a person types (decision
// 0097). Off until a person chooses it, and the default is the whole of the
// protection: a suggestion is a model call made from what somebody is halfway
// through typing, so nobody may be opted into it by a default.
inline constexpr char kComposerSuggestions[] = "taffy.ui.composer_suggestions";

// A bounded host-level visit ranking for the start page's tiles: one line per
// host carrying a count, a timestamp and a page title. Hosts only — never a
// path, query or full URL — and it stays on the device.
inline constexpr char kFrequentSites[] = "taffy.ui.frequent_sites";

// Bounded foreground dwell intervals keyed by registrable domain. This is a
// regular-profile value: private and assistant-created tabs never enter it.
inline constexpr char kTimeOnSites[] = "taffy.ui.time_on_sites";

// Provider records contain only authenticated ciphertext; the Android
// Keystore namespace is derived in trusted browser code from the profile
// identity and never depends on a mutable preference.
// What the browser told the core about each provider's credential: its auth
// method, the handle naming the sealed record, and the state last reported
// for it. No material and no second authority over what material exists — a
// handle whose record has gone resolves to no credential and the request is
// refused (decision 0117).
//
// It is held here because the core holds a credential for one generation and
// nothing refills it, so without this file a restart leaves every provider a
// person set up unreachable while its row still reads connected.
//
// Reading and writing it is `taffy/browser/model/
// provider_credential_announcement_store.h`; nothing else may name this
// string.
inline constexpr char kProviderCredentialAnnouncements[] =
    "taffy.model.provider_credential_announcements";

// The Android tab ids of the tabs Taffy created for a task, so that
// provenance survives a session restore (decision 0151).
//
// `AssistantCreatedTaskTabMarker` is WebContents user data and dies with the
// process, so before this register a restored tab carried no trustworthy
// ownership marker at all and was refused as a consent source for the life of
// the profile — which made "Ask about this page" impossible on every tab a
// person had open before the last restart.
//
// Ids only. Android never reuses a tab id within a profile, so a row that
// outlives its tab can only ever name that closed tab; nothing here is an
// address, a title or anything a page produced. Reading and writing it is
// `TaskSourceSelectionRegistry`; nothing else may name this string.
inline constexpr char kAssistantCreatedTaskTabs[] =
    "taffy.tasks.assistant_created_tabs";

inline constexpr char kProviderCredentialRecords[] =
    "taffy.security.provider_credential_records";
inline constexpr char kAccountSessionRecords[] =
    "taffy.security.account_session_records";
// Device and account sync-key bytes are individually authenticated by a
// dedicated Android Keystore key. This preference holds only their bounded
// ciphertext envelopes and content-free account/device/generation metadata.
inline constexpr char kSyncKeyRecords[] = "taffy.security.sync_key_records";

// Registers the profile-owned values consumed by Android today and by the
// future desktop projections. Call during profile-pref registration, before a
// PrefService is created.
void RegisterProfilePreferences(PrefRegistrySimple* registry);

}  // namespace taffy::profile_preferences

#endif  // TAFFY_BROWSER_PROFILE_PREFERENCES_H_

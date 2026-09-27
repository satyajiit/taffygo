// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/profile_preferences.h"

#include "components/prefs/pref_registry_simple.h"
#include "taffy/components/filtering/browser/filtering_prefs.h"

namespace taffy::profile_preferences {

void RegisterProfilePreferences(PrefRegistrySimple* registry) {
  // The filtering plane's preferences register beside their reader's names
  // (decision 0076); this function is where every taffy profile pref enters.
  taffy::filtering::RegisterFilteringPreferences(registry);
  registry->RegisterStringPref(kTheme, "SYSTEM");
  registry->RegisterStringPref(kAppLanguage, "ENGLISH");
  registry->RegisterStringPref(kRegionCode, "IN");
  registry->RegisterBooleanPref(kPseudoLocalization, false);
  registry->RegisterBooleanPref(kForceDarkWeb, false);
  registry->RegisterStringPref(kProviderRoute, "NOT_CONFIGURED");
  // Empty rather than a seeded row: no provider has a standing choice until a
  // person makes one, and a default row would be a choice nobody made.
  registry->RegisterDictionaryPref(kProviderModelPreferences);
  // Empty for the same reason, and for a second one: a seeded row here would
  // be an address the core could name that nobody typed, which is the whole of
  // what decision 0096 exists to prevent.
  registry->RegisterDictionaryPref(kCustomProviderEndpoints);
  // Empty because nothing has been announced to a core that has not started.
  // A seeded row would be a credential nobody saved, named to the core at the
  // first generation and refused there for want of a record behind it.
  registry->RegisterDictionaryPref(kProviderCredentialAnnouncements);
  registry->RegisterStringPref(kNotificationTopics, "TASK_PROGRESS");
  registry->RegisterBooleanPref(kOnboardingCompleted, false);
  registry->RegisterBooleanPref(kDiagnosticsOptIn, false);
  registry->RegisterBooleanPref(kComposerSuggestions, false);
  registry->RegisterStringPref(kFrequentSites, "");
  registry->RegisterStringPref(kTimeOnSites, "");
  // Empty because a profile that has never run a task created no tab. An
  // absent register is not the same as an unreadable one: absent reads as
  // "Taffy created nothing", unreadable refuses every restored tab.
  registry->RegisterListPref(kAssistantCreatedTaskTabs);
  registry->RegisterStringPref(kProviderCredentialRecords, "");
  registry->RegisterStringPref(kAccountSessionRecords, "");
  registry->RegisterStringPref(kSyncKeyRecords, "");
}

}  // namespace taffy::profile_preferences

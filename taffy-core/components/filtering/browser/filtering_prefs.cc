// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/components/filtering/browser/filtering_prefs.h"

#include "components/prefs/pref_registry_simple.h"

namespace taffy::filtering {

void RegisterFilteringPreferences(PrefRegistrySimple* registry) {
  registry->RegisterBooleanPref(kFilteringEnabledPref, true);
  registry->RegisterListPref(kFilteringSiteExceptionsPref);
  registry->RegisterInt64Pref(kFilteringBlockedTotalPref, 0);
  registry->RegisterInt64Pref(kFilteringWeekStartPref, 0);
  registry->RegisterInt64Pref(kFilteringBlockedThisWeekPref, 0);
  registry->RegisterListPref(kFilteringWeekSitesPref);
}

}  // namespace taffy::filtering

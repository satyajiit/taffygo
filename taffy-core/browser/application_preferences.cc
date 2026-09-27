// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/application_preferences.h"

#include <string>

#include "base/check.h"
#include "components/prefs/pref_registry_simple.h"

namespace taffy::application_preferences {

void RegisterLocalStatePreferences(PrefRegistrySimple* registry) {
  CHECK(registry);
  registry->RegisterStringPref(kBackupInstallationId, std::string());
  registry->RegisterDictionaryPref(kBackupRestoreProfileReservations);
}

}  // namespace taffy::application_preferences

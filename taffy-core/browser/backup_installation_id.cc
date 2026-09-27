// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/backup_installation_id.h"

#include <string>
#include <string_view>

#include "base/uuid.h"
#include "components/prefs/pref_service.h"
#include "taffy/browser/application_preferences.h"

namespace taffy {
namespace {

bool IsCanonicalUuidV4(std::string_view value) {
  const base::Uuid parsed = base::Uuid::ParseLowercase(value);
  return parsed.is_valid() && value[14] == '4' &&
         (value[19] == '8' || value[19] == '9' || value[19] == 'a' ||
          value[19] == 'b');
}

}  // namespace

std::optional<std::string> GetOrCreateBackupInstallationId(
    PrefService* local_state) {
  if (!local_state || !local_state->FindPreference(
                          application_preferences::kBackupInstallationId)) {
    return std::nullopt;
  }

  const std::string& stored =
      local_state->GetString(application_preferences::kBackupInstallationId);
  if (IsCanonicalUuidV4(stored)) {
    return stored;
  }

  // Empty is the first-run state. A nonempty malformed value is repaired in
  // exactly the same conservative direction: abandon that unauthenticated
  // identity and create a new one rather than let corrupt input enter a backup
  // manifest. SetString updates this PrefService synchronously and schedules
  // the Local State write, so every later caller in this process sees the same
  // value.
  std::string created = base::Uuid::GenerateRandomV4().AsLowercaseString();
  local_state->SetString(application_preferences::kBackupInstallationId,
                         created);
  return created;
}

}  // namespace taffy

// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_APPLICATION_PREFERENCES_H_
#define TAFFY_BROWSER_APPLICATION_PREFERENCES_H_

class PrefRegistrySimple;

namespace taffy::application_preferences {

// One non-secret identity for this installed application, outside every
// profile, account, and sync replica. The backup manifest carries it only as
// authenticated source-installation metadata; it grants no authority.
inline constexpr char kBackupInstallationId[] = "taffy.backup.installation_id";

// Browser-owned physical custody for a not-yet-visible restore profile. It
// contains no archive material and grants no stage, commit, publish, or delete
// authority. A malformed value is treated as a quarantine failure, never as
// an empty registry.
inline constexpr char kBackupRestoreProfileReservations[] =
    "taffy.backup.restore_profile_reservations";

// Registers TaffyGo's browser-wide values in Chromium Local State. This is
// called exactly once from Chrome's RegisterLocalState(), before its
// PrefService is created.
void RegisterLocalStatePreferences(PrefRegistrySimple* registry);

}  // namespace taffy::application_preferences

#endif  // TAFFY_BROWSER_APPLICATION_PREFERENCES_H_

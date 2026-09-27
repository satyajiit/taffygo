// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_BACKUP_INSTALLATION_ID_H_
#define TAFFY_BROWSER_BACKUP_INSTALLATION_ID_H_

#include <optional>
#include <string>

class PrefService;

namespace taffy {

// Returns the validated browser-wide backup source identity, creating and
// persisting one lowercase UUID v4 when Local State has none. An unregistered
// or unavailable service fails closed. A malformed stored value is replaced
// with a new identity; it is never forwarded as authenticated metadata.
std::optional<std::string> GetOrCreateBackupInstallationId(
    PrefService* local_state);

}  // namespace taffy

#endif  // TAFFY_BROWSER_BACKUP_INSTALLATION_ID_H_

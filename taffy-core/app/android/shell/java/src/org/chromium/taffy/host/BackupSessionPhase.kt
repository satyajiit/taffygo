// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.host

enum class BackupSessionPhase {
    KEY,
    CREATE_CONFIRMED,
    EXPORT_PREPARING,
    EXPORT_READY,
    EXPORT_COPYING,
    IMPORT_READY,
    IMPORT_COPYING,
    IMPORT_VERIFIED,
    RESTORE_PREPARING,
    RESTORE_REVIEW,
    RESTORE_STAGING,
    RESTORE_STAGED,
    RESTORE_COMMITTING,
    RESTORE_CANDIDATE,
    RESTORE_RESOLVING,
    CLOSED,
}

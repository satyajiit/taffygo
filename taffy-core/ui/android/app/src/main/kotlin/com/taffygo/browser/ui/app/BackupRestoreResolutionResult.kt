// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.app

enum class BackupRestoreResolutionResult {
    /** Profile metadata is published, but this result never activates or selects the profile. */
    PUBLISHED,
    VERIFIED_DELETED,
    /**
     * Native proved this choice did not complete. The same candidate remains hidden; only a new
     * explicit choice may ask native for fresh authority. This result never causes an automatic
     * retry.
     */
    DEFINITELY_NOT_COMPLETED,
    RECOVERY_REQUIRED,
    /** The request was not authorized; the same hidden candidate remains available for a choice. */
    REFUSED,
    /** No physical action began; the same hidden candidate remains available for a choice. */
    UNAVAILABLE,
}

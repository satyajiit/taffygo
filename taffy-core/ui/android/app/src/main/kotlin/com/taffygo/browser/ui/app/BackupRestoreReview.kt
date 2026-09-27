// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.app

/**
 * A live, window-bound presentation of one native restore plan.
 *
 * The implementation is intentionally opaque. It must not be placed in saved state: native
 * custody retains every operation, reservation, profile, digest and authorization identity.
 */
interface BackupRestoreReview {
    val summary: BackupRestoreSummary
}

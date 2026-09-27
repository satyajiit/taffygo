// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.app

/** Counts for one selected record class. No record identity or content crosses this seam. */
data class BackupRestoreClassSummary(
    val contentClass: BackupWindowHost.ContentClass,
    val createCount: Int,
    val deletionCount: Int,
    val alreadyPresentCount: Int,
    val keepNewerCount: Int,
    val blockedCount: Int,
    val conflictCount: Int,
)

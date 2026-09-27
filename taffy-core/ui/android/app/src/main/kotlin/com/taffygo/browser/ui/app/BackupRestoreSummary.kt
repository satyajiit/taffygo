// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.app

/** Bounded, content-free information that a person can review before staging. */
data class BackupRestoreSummary(
    val targetProfileLabel: String,
    /** Exactly the selected classes, in [BackupWindowHost.ContentClass] order. */
    val selectedClasses: List<BackupRestoreClassSummary>,
    val hasConflicts: Boolean,
    /** False when this build cannot honestly complete every choice in the plan. */
    val canStage: Boolean,
)

// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.app

/** Presentation of a live restore ceremony; never an operation, digest, or recovery capability. */
data class BackupRestoreUiState(
    val step: Step = Step.PLANNING,
    val summary: BackupRestoreSummary? = null,
    val choiceNotCompleted: Boolean = false,
    val cleanupOnly: Boolean = false,
    val recovered: Boolean = false,
    val recoveryReason: BackupRestoreDiscoveryResult.Reason? = null,
) {
    enum class Step {
        DISCOVERING, PLANNING, REVIEW, STAGING, STAGED, COMMITTING, CANDIDATE, CLEANUP_REQUIRED,
        CONFIRM_DISCARD, RESOLVING, NEEDS_RECOVERY,
    }

    val mayCancelPreparation: Boolean get() = step in setOf(
        Step.DISCOVERING, Step.PLANNING, Step.REVIEW, Step.STAGING, Step.STAGED,
    )
}

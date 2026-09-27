// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.app

/** Observation of interrupted work, never a replayed key, import, commit or resolution choice. */
sealed interface BackupRestoreDiscoveryResult {
    /** Fresh window-owned review; only a new explicit choice may ask native for authority. */
    data class Ready(val review: BackupRestoreReview, val cleanupOnly: Boolean) : BackupRestoreDiscoveryResult
    data object None : BackupRestoreDiscoveryResult
    data object SourceUnavailable : BackupRestoreDiscoveryResult
    data object Unavailable : BackupRestoreDiscoveryResult

    /** Returned only after exact terminal verification and retirement of its recovery record. */
    data object AlreadyKept : BackupRestoreDiscoveryResult
    data object AlreadyDiscarded : BackupRestoreDiscoveryResult

    data class RecoveryRequired(val reason: Reason) : BackupRestoreDiscoveryResult

    /** These states carry no profile name, summary or action. */
    enum class Reason {
        PRECOMMIT, PRESENTATION_UNAVAILABLE, SCHEMA_MISMATCH, OUTCOME_UNKNOWN, CUSTODY_AMBIGUOUS,
    }
}

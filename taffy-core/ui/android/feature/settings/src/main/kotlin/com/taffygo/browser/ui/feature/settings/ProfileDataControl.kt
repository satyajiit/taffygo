// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import kotlinx.coroutines.flow.StateFlow

/**
 * One profile-wide seam for a personal-data export and Android sandbox erasure.
 *
 * A shipping adapter may report export available only when it can take one
 * complete, bounded snapshot of every in-scope store. Deletion is one OS-owned
 * operation; adapters must not manufacture success from several partial clears.
 */
interface ProfileDataControl {
    val availability: StateFlow<Availability>

    /** Render once, then hand the exact bytes to the user-chosen document writer. */
    suspend fun export(write: suspend (ByteArray) -> Boolean): ExportResult

    /** Revoke live authority and ask Android to erase this application's sandbox. */
    suspend fun deleteApplicationData(): DeletionResult

    data class Availability(
        val exportAvailable: Boolean,
        val deleteAvailable: Boolean,
    )

    enum class ExportResult { COMPLETED, UNAVAILABLE, FAILED }

    /** Android accepts an asynchronous, process-killing request; it never reports completion. */
    enum class DeletionResult { STARTED, UNAVAILABLE, FAILED }
}

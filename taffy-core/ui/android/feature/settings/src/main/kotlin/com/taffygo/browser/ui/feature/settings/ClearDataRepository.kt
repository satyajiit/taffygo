// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

/**
 * Clearing chosen kinds for a chosen range.
 *
 * Workspaces, Library, Memory, and saved sign-ins are never in the set.
 * Unavailable does not report success and does not empty a list it did not
 * clear.
 */
interface ClearDataRepository {
    val available: Boolean
    val supportedClasses: Set<ClearBrowsingDataUiState.DataClass>

    suspend fun clear(
        range: ClearBrowsingDataUiState.Range,
        classes: Set<ClearBrowsingDataUiState.DataClass>,
    ): Boolean
}

internal class UnavailableClearDataRepository : ClearDataRepository {
    override val available: Boolean = false
    override val supportedClasses: Set<ClearBrowsingDataUiState.DataClass> = emptySet()

    override suspend fun clear(
        range: ClearBrowsingDataUiState.Range,
        classes: Set<ClearBrowsingDataUiState.DataClass>,
    ): Boolean = false
}

internal class EmptyClearDataRepository : ClearDataRepository {
    override val available: Boolean = false
    override val supportedClasses: Set<ClearBrowsingDataUiState.DataClass> = emptySet()

    override suspend fun clear(
        range: ClearBrowsingDataUiState.Range,
        classes: Set<ClearBrowsingDataUiState.DataClass>,
    ): Boolean = false
}

// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow

/**
 * A visit store this build cannot read.
 *
 * Unavailable is not empty: empty would claim the store was asked and had
 * nothing. This claims the store is missing, so the screen must not mint
 * sample visits.
 */
class UnavailableHistoryRepository : HistoryRepository {
    override val snapshot: StateFlow<HistorySnapshot> =
        MutableStateFlow(HistorySnapshot.Unavailable).asStateFlow()

    override suspend fun open(id: HistoryVisit.Id): Boolean = false

    override suspend fun delete(id: HistoryVisit.Id) = Unit
}

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
 * A visit store that exists and has nothing in it.
 *
 * Ready-and-empty is the honest reading when this phone has not opened a
 * page, or when every visit has been cleared. It never invents a row so the
 * list looks inhabited.
 */
class EmptyHistoryRepository : HistoryRepository {
    override val snapshot: StateFlow<HistorySnapshot> =
        MutableStateFlow(HistorySnapshot.Ready(emptyList())).asStateFlow()

    override suspend fun open(id: HistoryVisit.Id): Boolean = false

    override suspend fun delete(id: HistoryVisit.Id) = Unit
}

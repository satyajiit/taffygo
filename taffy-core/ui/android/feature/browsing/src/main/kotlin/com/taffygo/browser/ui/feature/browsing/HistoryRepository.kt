// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import kotlinx.coroutines.flow.StateFlow

/**
 * The visit list for screen SCR-201.
 *
 * Private tabs are never stored here. Pages Taffy opened for a task are not
 * the person's history either, unless the person took the tab over and then
 * opened the page themselves.
 */
interface HistoryRepository {
    /** The current visit list, or the honest absence of one. */
    val snapshot: StateFlow<HistorySnapshot>

    /** Open the exact address still bound to [id], or refuse a stale identifier. */
    suspend fun open(id: HistoryVisit.Id): Boolean

    /** Remove one visit the person asked to drop. */
    suspend fun delete(id: HistoryVisit.Id)
}

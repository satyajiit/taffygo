// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import com.taffygo.browser.ui.core.model.SavedFlowReview
import com.taffygo.browser.ui.core.model.TabId
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.asStateFlow

/** A window-local request to show fresh offers after an explicitly reviewed manual navigation. */
class SavedFlowPageRequests {
    private val pending = MutableStateFlow<Request?>(null)
    val state = pending.asStateFlow()
    fun show(goal: String, review: SavedFlowReview, tabId: TabId) { pending.value = Request(goal, review, tabId) }
    fun consumed(request: Request) { pending.compareAndSet(request, null) }
    data class Request(val goal: String, val review: SavedFlowReview, val tabId: TabId)
}

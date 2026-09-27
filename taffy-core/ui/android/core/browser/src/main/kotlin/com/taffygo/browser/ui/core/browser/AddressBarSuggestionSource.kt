// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.browser

import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow

private val NO_SOURCE_REVISIONS: StateFlow<Long> = MutableStateFlow(0L).asStateFlow()

/**
 * Pre-indexed local page suggestions for one browser window.
 *
 * Implementations observe their stores away from the rendering thread. A call
 * is synchronous because it reads only that bounded index: it performs no
 * database query, model traversal, disk access, or unbounded collection scan.
 * Private-mode filtering and deletion updates are part of this interface, not
 * optional work for its caller.
 */
interface AddressBarSuggestionSource {
    /** Changes after a replacement index is ready for synchronous reads. */
    val revision: StateFlow<Long>
        get() = NO_SOURCE_REVISIONS

    /** At most [MAX_RESULTS] stable, deduplicated results in relevance order. */
    fun suggestions(input: String): List<AddressBarPageSuggestion>

    companion object {
        const val MAX_RESULTS: Int = 5

        /** Fail-closed adapter for tests or a window with no readable stores. */
        val NONE: AddressBarSuggestionSource = object : AddressBarSuggestionSource {
            override fun suggestions(input: String): List<AddressBarPageSuggestion> = emptyList()
        }
    }
}

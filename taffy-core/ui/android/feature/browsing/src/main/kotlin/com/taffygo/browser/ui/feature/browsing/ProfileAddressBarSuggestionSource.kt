// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import com.taffygo.browser.ui.core.browser.AddressBarPageSuggestion
import com.taffygo.browser.ui.core.browser.AddressBarSuggestionSource
import com.taffygo.browser.ui.core.model.Tab
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.flow.launchIn
import kotlinx.coroutines.flow.onEach

/** Window adapter that keeps address-bar page lookup off the rendering thread. */
internal class ProfileAddressBarSuggestionSource(
    tabs: StateFlow<List<Tab>>,
    history: StateFlow<HistorySnapshot>,
    bookmarks: StateFlow<BookmarksSnapshot>,
    scope: CoroutineScope,
) : AddressBarSuggestionSource {
    @Volatile
    private var index = AddressBarSuggestionIndex.empty()
    private val revisionState = MutableStateFlow(0L)

    override val revision: StateFlow<Long> = revisionState.asStateFlow()

    init {
        combine(tabs, history, bookmarks) { currentTabs, currentHistory, currentBookmarks ->
            AddressBarSuggestionIndex.build(currentTabs, currentHistory, currentBookmarks)
        }.onEach { rebuilt ->
            index = rebuilt
            revisionState.value += 1L
        }.launchIn(scope)
    }

    override fun suggestions(input: String): List<AddressBarPageSuggestion> =
        index.suggestions(input)
}

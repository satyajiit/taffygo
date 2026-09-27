// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.taffygo.browser.ui.core.model.TabId
import javax.inject.Inject
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.flow.onEach
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch

/** Add pages sheet: open user tabs, search, tick, confirm. */
class AttachPagesViewModel @Inject constructor(
    private val pages: AskPagesRepository,
) : ViewModel() {

    private val internalState = MutableStateFlow(AttachPagesUiState())
    private var alreadyAttached: List<TabId> = emptyList()
    private var collecting = false

    /** What the Add pages sheet renders. */
    val state: StateFlow<AttachPagesUiState> = internalState.asStateFlow()

    /** Open or re-open the sheet with the pages already on the composer. */
    fun start(alreadyAttached: List<TabId>) {
        this.alreadyAttached = alreadyAttached
        internalState.value = attachPagesFromSnapshot(
            snapshot = askPagesSnapshot(pages.tabs.value, pages.status),
            alreadyAttached = alreadyAttached,
            artwork = pages.tabArtwork.value,
            siteMarks = pages.siteMarks.value,
        )
        if (collecting) return
        collecting = true
        viewModelScope.launch {
            combine(
                pages.tabs.onEach { tabs ->
                    pages.requestSiteMarks(tabs.filter(::isEligibleAskTab).map { it.host }.toSet())
                },
                pages.tabArtwork,
                pages.siteMarks,
            ) { tabs, artwork, marks ->
                val current = internalState.value
                attachPagesFromSnapshot(
                    snapshot = askPagesSnapshot(tabs, pages.status),
                    alreadyAttached = this@AttachPagesViewModel.alreadyAttached,
                    query = current.query,
                    previousTicks = current.tickedIds,
                    artwork = artwork,
                    siteMarks = marks,
                )
            }.collect { internalState.value = it }
        }
    }

    /** Act on something the user did. */
    fun onIntent(intent: AttachPagesIntent) {
        internalState.update { reduceAttachPages(it, intent) }
    }
}

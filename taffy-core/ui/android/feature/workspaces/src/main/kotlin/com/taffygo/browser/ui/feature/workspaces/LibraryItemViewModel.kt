// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.workspaces

import androidx.lifecycle.SavedStateHandle
import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.taffygo.browser.ui.core.analytics.AnalyticsClient
import com.taffygo.browser.ui.core.analytics.AnalyticsEvent
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyNavigator
import javax.inject.Inject
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.flow.stateIn
import kotlinx.coroutines.launch

/**
 * Screen SCR-503's one source of truth.
 *
 * Remove is accepted so the control exists, and it does nothing while the port
 * cannot mutate — it never reports a deletion it did not make.
 */
class LibraryItemViewModel @Inject constructor(
    private val library: LibraryRepository,
    private val analytics: AnalyticsClient,
    savedState: SavedStateHandle,
) : ViewModel() {

    private val collectionId = savedState.get<String>(TaffyDestination.COLLECTION_ID).orEmpty()
    private val itemId = savedState.get<String>(TaffyDestination.ITEM_ID).orEmpty()

    /** What screen SCR-503 renders. */
    val state: StateFlow<LibraryItemUiState> = library.snapshot
        .map { snapshot ->
            projectLibraryItem(snapshot, collectionId, itemId, library.canMutate)
        }
        .stateIn(
            scope = viewModelScope,
            started = SharingStarted.WhileSubscribed(SUBSCRIPTION_TIMEOUT_MILLIS),
            initialValue = projectLibraryItem(
                library.snapshot.value,
                collectionId,
                itemId,
                library.canMutate,
            ),
        )

    /** Act on something the person did. */
    fun onIntent(intent: LibraryItemIntent, navigator: TaffyNavigator) {
        when (intent) {
            is LibraryItemIntent.OpenRelated ->
                navigator.goTo(TaffyDestination.LibraryItem(intent.collectionId, intent.itemId))
            LibraryItemIntent.RemoveItem -> viewModelScope.launch {
                val item = (library.snapshot.value as? LibraryRepository.Snapshot.Ready)
                    ?.collections
                    ?.firstOrNull { it.id == collectionId }
                    ?.items
                    ?.firstOrNull { it.id == itemId }
                    ?: return@launch
                library.removeItem(item.id, item.revision)
            }
        }
    }

    /** Record that this screen was shown. */
    fun onShown() {
        analytics.record(
            AnalyticsEvent.ScreenShown(
                TaffyDestination.LibraryItem(collectionId, itemId).screenId,
            ),
        )
    }

    private companion object {
        const val SUBSCRIPTION_TIMEOUT_MILLIS = 5_000L
    }
}

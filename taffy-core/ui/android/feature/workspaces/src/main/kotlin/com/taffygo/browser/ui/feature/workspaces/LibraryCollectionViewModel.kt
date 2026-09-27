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
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.flow.stateIn
import kotlinx.coroutines.launch

/**
 * Screen SCR-502's one source of truth.
 *
 * Export opens the trusted Library artifact flow. A collection is backed by
 * its saved workspace, so rename and delete use that workspace's exact
 * revision and published confirmation challenge.
 */
class LibraryCollectionViewModel @Inject constructor(
    private val library: LibraryRepository,
    private val analytics: AnalyticsClient,
    private val savedState: SavedStateHandle,
) : ViewModel() {

    private val collectionId = savedState.get<String>(TaffyDestination.COLLECTION_ID).orEmpty()
    private val query = MutableStateFlow(savedState.get<String>(QUERY_KEY).orEmpty())
    private var searchJob: Job? = null

    init {
        requestSearch(query.value)
    }

    /** What screen SCR-502 renders. */
    val state: StateFlow<LibraryCollectionUiState> =
        combine(library.snapshot, query) { snapshot, typed ->
            projectLibraryCollection(
                snapshot,
                collectionId,
                typed,
                library.canMutate,
                library.canManageCollections,
            )
        }
            .stateIn(
                scope = viewModelScope,
                started = SharingStarted.WhileSubscribed(SUBSCRIPTION_TIMEOUT_MILLIS),
                initialValue = projectLibraryCollection(
                    library.snapshot.value,
                    collectionId,
                    query.value,
                    library.canMutate,
                    library.canManageCollections,
                ),
            )

    /** Act on something the person did. */
    fun onIntent(intent: LibraryCollectionIntent, navigator: TaffyNavigator) {
        when (intent) {
            is LibraryCollectionIntent.QueryChanged -> {
                query.value = intent.query
                savedState[QUERY_KEY] = intent.query
                requestSearch(intent.query)
            }
            is LibraryCollectionIntent.OpenItem ->
                navigator.goTo(TaffyDestination.LibraryItem(collectionId, intent.itemId))
            LibraryCollectionIntent.RemoveCollection,
            -> Unit
            is LibraryCollectionIntent.ApproveRefresh -> viewModelScope.launch {
                library.startRefresh(intent.preview)
            }
            LibraryCollectionIntent.Export ->
                navigator.goTo(TaffyDestination.LibraryExport(collectionId))
            is LibraryCollectionIntent.RenameCollection -> viewModelScope.launch {
                library.renameCollection(
                    collectionId,
                    intent.expectedRevision,
                    intent.displayName,
                )
            }
            is LibraryCollectionIntent.DeleteCollection -> viewModelScope.launch {
                library.deleteCollection(
                    collectionId,
                    intent.expectedRevision,
                    intent.confirmationToken,
                )
            }
        }
    }

    /** Record that this screen was shown. */
    fun onShown() {
        analytics.record(
            AnalyticsEvent.ScreenShown(TaffyDestination.LibraryCollection(collectionId).screenId),
        )
    }

    private fun requestSearch(value: String) {
        searchJob?.cancel()
        if (value.trim().isEmpty()) return
        searchJob = viewModelScope.launch {
            delay(SEARCH_DEBOUNCE_MILLIS)
            library.search(value)
        }
    }

    private companion object {
        const val SUBSCRIPTION_TIMEOUT_MILLIS = 5_000L
        const val QUERY_KEY = "library_collection_query"
        const val SEARCH_DEBOUNCE_MILLIS = 250L
    }
}

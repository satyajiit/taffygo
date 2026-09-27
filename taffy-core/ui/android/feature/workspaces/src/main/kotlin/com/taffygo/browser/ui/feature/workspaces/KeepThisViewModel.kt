// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.workspaces

import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.taffygo.browser.ui.core.analytics.AnalyticsClient
import com.taffygo.browser.ui.core.analytics.AnalyticsEvent
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyNavigator
import javax.inject.Inject
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.flow.stateIn

/**
 * Screen SCR-504's one source of truth.
 *
 * Keep is accepted so the control exists, and it adds nothing while the port
 * cannot — the disconnected sentence stays on screen.
 */
class KeepThisViewModel @Inject constructor(
    private val library: LibraryRepository,
    private val analytics: AnalyticsClient,
) : ViewModel() {

    private val selectedCollectionId = MutableStateFlow<String?>(null)
    private val selectedKind = MutableStateFlow<KeepThisKind?>(null)

    /** What screen SCR-504 renders. */
    val state: StateFlow<KeepThisUiState> =
        combine(library.snapshot, selectedCollectionId, selectedKind) { snapshot, collection, kind ->
            // This context-free sheet cannot name an exact saved workspace
            // revision and fact. Fact rows own the live Keep action.
            projectKeepThis(snapshot, collection, kind, canKeep = false)
        }
            .stateIn(
                scope = viewModelScope,
                started = SharingStarted.WhileSubscribed(SUBSCRIPTION_TIMEOUT_MILLIS),
                initialValue = projectKeepThis(
                    library.snapshot.value,
                    selectedCollectionId.value,
                    selectedKind.value,
                    canKeep = false,
                ),
            )

    /** Act on something the person did. */
    fun onIntent(intent: KeepThisIntent, navigator: TaffyNavigator) {
        val reduced = reduceKeepThis(state.value, intent)
        selectedCollectionId.value = reduced.selectedCollectionId
        selectedKind.value = reduced.selectedKind
        when (intent) {
            KeepThisIntent.Dismiss -> navigator.goBack()
            KeepThisIntent.Keep,
            is KeepThisIntent.SelectCollection,
            is KeepThisIntent.SelectKind,
            -> Unit
        }
    }

    /** Record that this screen was shown. */
    fun onShown() {
        analytics.record(AnalyticsEvent.ScreenShown(TaffyDestination.KeepThis.screenId))
    }

    private companion object {
        const val SUBSCRIPTION_TIMEOUT_MILLIS = 5_000L
    }
}

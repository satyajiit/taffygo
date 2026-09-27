// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import androidx.lifecycle.SavedStateHandle
import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.taffygo.browser.ui.core.analytics.AnalyticsClient
import com.taffygo.browser.ui.core.analytics.AnalyticsEvent
import com.taffygo.browser.ui.core.ui.TaffyDestination
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.flow.stateIn

/** Screen SCR-411's one source of truth. */
class TimeOnSitesViewModel(
    repository: TimeOnSitesRepository,
    private val analytics: AnalyticsClient,
    private val savedState: SavedStateHandle,
) : ViewModel() {

    private val range = MutableStateFlow(
        savedState.get<String>(RANGE_KEY)
            ?.let { runCatching { TimeOnSitesUiState.Range.valueOf(it) }.getOrNull() }
            ?: TimeOnSitesUiState.Range.TODAY,
    )

    /** What screen SCR-411 renders. */
    val state: StateFlow<TimeOnSitesUiState> =
        combine(repository.snapshot, range, ::projectTimeOnSites)
            .stateIn(
                scope = viewModelScope,
                started = SharingStarted.WhileSubscribed(SUBSCRIPTION_TIMEOUT_MILLIS),
                initialValue = projectTimeOnSites(repository.snapshot.value, range.value),
            )

    /** Act on something the user did. */
    fun onIntent(intent: TimeOnSitesIntent) {
        val next = reduceTimeOnSites(state.value, intent)
        if (next.range != range.value) {
            range.value = next.range
            savedState[RANGE_KEY] = next.range.name
        }
    }

    /** Record that this screen was shown. */
    fun onShown() {
        analytics.record(AnalyticsEvent.ScreenShown(TaffyDestination.TimeOnSites.screenId))
    }

    private companion object {
        const val RANGE_KEY = "time_on_sites_range"
        const val SUBSCRIPTION_TIMEOUT_MILLIS = 5_000L
    }
}

// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.taffygo.browser.ui.core.analytics.AnalyticsClient
import com.taffygo.browser.ui.core.analytics.AnalyticsEvent
import com.taffygo.browser.ui.core.browser.SearchEngineRepository
import com.taffygo.browser.ui.core.preferences.UserPreferencesRepository
import com.taffygo.browser.ui.core.ui.TaffyDestination
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.flow.stateIn
import kotlinx.coroutines.launch

/** The search-engine picker on screen SCR-402. */
class SearchEngineViewModel(
    private val searchEngines: SearchEngineRepository,
    preferences: UserPreferencesRepository,
    private val analytics: AnalyticsClient,
) : ViewModel() {

    val state: StateFlow<SearchEngineUiState> = combine(
        searchEngines.selectedId,
        preferences.preferences,
    ) { selectedId, prefs ->
        projectSearchEngine(selectedId, prefs.regionCode)
    }.stateIn(
        scope = viewModelScope,
        started = SharingStarted.WhileSubscribed(SUBSCRIPTION_TIMEOUT_MILLIS),
        initialValue = projectSearchEngine(
            searchEngines.selectedId.value,
            preferences.preferences.value.regionCode,
        ),
    )

    fun onIntent(intent: SearchEngineIntent) {
        when (intent) {
            is SearchEngineIntent.Select -> viewModelScope.launch {
                searchEngines.select(intent.id)
            }
            SearchEngineIntent.Dismiss -> Unit
        }
    }

    fun onShown() {
        analytics.record(AnalyticsEvent.ScreenShown(TaffyDestination.General.screenId))
    }

    private companion object {
        const val SUBSCRIPTION_TIMEOUT_MILLIS = 5_000L
    }
}

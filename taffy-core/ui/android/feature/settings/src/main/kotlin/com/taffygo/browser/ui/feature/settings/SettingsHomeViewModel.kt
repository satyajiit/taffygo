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
import com.taffygo.browser.ui.core.ui.TaffyNavigator
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.combine
import com.taffygo.browser.ui.core.preferences.LocalProfileRepository
import kotlinx.coroutines.flow.stateIn

/** Screen SCR-401's one source of truth. */
class SettingsHomeViewModel(
    private val analytics: AnalyticsClient,
    private val savedState: SavedStateHandle,
    week: BlockingWeekRepository,
    profile: LocalProfileRepository,
) : ViewModel() {

    private val local = MutableStateFlow(
        restoreSettingsHome(
            query = savedState.get<String>(QUERY_KEY).orEmpty(),
            searchOpen = savedState.get<Boolean>(SEARCH_OPEN_KEY),
        ),
    )

    /** What screen SCR-401 renders. */
    val state: StateFlow<SettingsHomeUiState> = combine(
        local,
        week.snapshot,
        profile.profile,
    ) { home, weekSnap, person ->
        projectSettingsHome(home, weekSnap, person)
    }.stateIn(
        scope = viewModelScope,
        started = SharingStarted.WhileSubscribed(SUBSCRIPTION_TIMEOUT_MILLIS),
        initialValue = projectSettingsHome(
            local.value,
            week.snapshot.value,
            profile.profile.value,
        ),
    )

    /** Act on something the user did. */
    fun onIntent(intent: SettingsHomeIntent, navigator: TaffyNavigator) {
        local.value = reduceSettingsHome(local.value, intent)
        savedState[QUERY_KEY] = local.value.query
        savedState[SEARCH_OPEN_KEY] = local.value.searchOpen
        if (intent is SettingsHomeIntent.Open) navigator.goTo(intent.destination)
    }

    /** Record that this screen was shown. */
    fun onShown() {
        analytics.record(AnalyticsEvent.ScreenShown(TaffyDestination.SettingsHome.screenId))
    }

    private companion object {
        const val QUERY_KEY = "settings_query"
        const val SEARCH_OPEN_KEY = "settings_search_open"
        const val SUBSCRIPTION_TIMEOUT_MILLIS = 5_000L
    }
}

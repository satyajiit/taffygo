// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.onboarding

import androidx.lifecycle.ViewModel
import androidx.lifecycle.SavedStateHandle
import androidx.lifecycle.viewModelScope
import com.taffygo.browser.ui.core.analytics.AnalyticsClient
import com.taffygo.browser.ui.core.analytics.AnalyticsEvent
import com.taffygo.browser.ui.core.preferences.UserPreferencesRepository
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyNavigator
import javax.inject.Inject
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.flow.stateIn
import kotlinx.coroutines.launch

/** Screen SCR-006's one source of truth. */
class LanguageRegionViewModel @Inject constructor(
    private val preferences: UserPreferencesRepository,
    private val analytics: AnalyticsClient,
    private val savedState: SavedStateHandle,
) : ViewModel() {

    private val query = MutableStateFlow(savedState.get<String>(SEARCH_KEY).orEmpty())
    private val regionQuery = MutableStateFlow(savedState.get<String>(REGION_SEARCH_KEY).orEmpty())
    private val regionPickerVisible = MutableStateFlow(false)

    /** What screen SCR-006 renders. */
    val state: StateFlow<LanguageRegionUiState> = combine(
        preferences.preferences,
        query,
        regionQuery,
        regionPickerVisible,
    ) { settings, search, countrySearch, pickerVisible ->
        LanguageRegionUiState(
            appLanguage = settings.appLanguage,
            regionCode = settings.regionCode,
            searchQuery = search,
            regionPickerVisible = pickerVisible,
            regionSearchQuery = countrySearch,
        )
    }
        .stateIn(
            scope = viewModelScope,
            started = SharingStarted.WhileSubscribed(SUBSCRIPTION_TIMEOUT_MILLIS),
            initialValue = LanguageRegionUiState(
                appLanguage = preferences.preferences.value.appLanguage,
                regionCode = preferences.preferences.value.regionCode,
                searchQuery = query.value,
                regionPickerVisible = regionPickerVisible.value,
                regionSearchQuery = regionQuery.value,
            ),
        )

    /** Act on something the user did. */
    fun onIntent(intent: LanguageRegionIntent, navigator: TaffyNavigator) {
        when (intent) {
            is LanguageRegionIntent.SearchChanged -> {
                query.value = intent.query
                savedState[SEARCH_KEY] = intent.query
            }
            LanguageRegionIntent.OpenRegionPicker -> regionPickerVisible.value = true
            LanguageRegionIntent.CloseRegionPicker -> regionPickerVisible.value = false
            is LanguageRegionIntent.RegionSearchChanged -> {
                regionQuery.value = intent.query
                savedState[REGION_SEARCH_KEY] = intent.query
            }
            is LanguageRegionIntent.ChooseRegion -> viewModelScope.launch {
                preferences.setRegionCode(intent.regionCode)
                regionPickerVisible.value = false
                regionQuery.value = ""
                savedState[REGION_SEARCH_KEY] = ""
            }
            is LanguageRegionIntent.ChooseLanguage -> viewModelScope.launch {
                preferences.setAppLanguage(intent.language)
            }
            LanguageRegionIntent.Done -> navigator.goBack()
        }
    }

    /** Record that this screen was shown. */
    fun onShown() {
        analytics.record(AnalyticsEvent.ScreenShown(TaffyDestination.LanguageRegion.screenId))
    }

    private companion object {
        const val SUBSCRIPTION_TIMEOUT_MILLIS = 5_000L
        const val SEARCH_KEY = "language_search"
        const val REGION_SEARCH_KEY = "region_search"
    }
}

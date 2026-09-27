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
import com.taffygo.browser.ui.core.preferences.UserPreferencesRepository
import com.taffygo.browser.ui.core.ui.TaffyDestination
import javax.inject.Inject
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.flow.stateIn
import kotlinx.coroutines.launch

/** Screen SCR-407's one source of truth. */
class AppearanceViewModel @Inject constructor(
    private val preferences: UserPreferencesRepository,
    private val analytics: AnalyticsClient,
) : ViewModel() {

    private val pickerVisible = MutableStateFlow(false)
    private val regionQuery = MutableStateFlow("")
    private val selectedTab = MutableStateFlow(AppearanceTab.THEME)

    /** What screen SCR-407 renders. */
    val state: StateFlow<AppearanceUiState> = combine(
        preferences.preferences,
        pickerVisible,
        regionQuery,
        selectedTab,
    ) { settings, visible, query, tab ->
        AppearanceUiState(
            tab = tab,
            theme = settings.theme,
            appLanguage = settings.appLanguage,
            regionCode = settings.regionCode,
            pseudoLocalization = settings.pseudoLocalization,
            forceDarkWeb = settings.forceDarkWeb,
            regionPickerVisible = visible,
            regionSearchQuery = query,
        )
    }
        .stateIn(
            scope = viewModelScope,
            started = SharingStarted.WhileSubscribed(SUBSCRIPTION_TIMEOUT_MILLIS),
            initialValue = AppearanceUiState(
                tab = selectedTab.value,
                theme = preferences.preferences.value.theme,
                appLanguage = preferences.preferences.value.appLanguage,
                regionCode = preferences.preferences.value.regionCode,
                pseudoLocalization = preferences.preferences.value.pseudoLocalization,
                forceDarkWeb = preferences.preferences.value.forceDarkWeb,
                regionPickerVisible = pickerVisible.value,
                regionSearchQuery = regionQuery.value,
            ),
        )

    /** Act on something the user did. */
    fun onIntent(intent: AppearanceIntent) {
        val before = state.value
        val reduced = reduceAppearance(before, intent)
        pickerVisible.value = reduced.regionPickerVisible
        regionQuery.value = reduced.regionSearchQuery
        selectedTab.value = reduced.tab
        viewModelScope.launch {
            when (intent) {
                is AppearanceIntent.ChooseTheme -> preferences.setTheme(reduced.theme)
                is AppearanceIntent.ChooseLanguage ->
                    preferences.setAppLanguage(reduced.appLanguage)
                is AppearanceIntent.ChooseRegion ->
                    if (reduced.regionCode != before.regionCode) {
                        preferences.setRegionCode(reduced.regionCode)
                    }
                AppearanceIntent.TogglePseudoLocalization ->
                    preferences.setPseudoLocalization(reduced.pseudoLocalization)
                AppearanceIntent.ToggleForceDarkWeb ->
                    preferences.setForceDarkWeb(reduced.forceDarkWeb)
                is AppearanceIntent.SelectTab,
                AppearanceIntent.OpenRegionPicker,
                AppearanceIntent.CloseRegionPicker,
                is AppearanceIntent.RegionSearchChanged,
                -> Unit
            }
        }
    }

    /** Record that this screen was shown. */
    fun onShown() {
        analytics.record(AnalyticsEvent.ScreenShown(TaffyDestination.Appearance.screenId))
    }

    private companion object {
        const val SUBSCRIPTION_TIMEOUT_MILLIS = 5_000L
    }
}

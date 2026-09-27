// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.onboarding

import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.taffygo.browser.ui.core.analytics.AnalyticsClient
import com.taffygo.browser.ui.core.analytics.AnalyticsEvent
import com.taffygo.browser.ui.core.preferences.UserPreferencesRepository
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyNavigator
import javax.inject.Inject
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.flow.stateIn
import kotlinx.coroutines.launch

/** Screen SCR-001's one source of truth. */
class WelcomeViewModel @Inject constructor(
    private val preferences: UserPreferencesRepository,
    private val analytics: AnalyticsClient,
) : ViewModel() {

    /** What screen SCR-001 renders. */
    val state: StateFlow<WelcomeUiState> = preferences.preferences
        .map {
            WelcomeUiState(
                appLanguage = it.appLanguage,
                regionCode = it.regionCode,
                theme = it.theme,
            )
        }
        .stateIn(
            scope = viewModelScope,
            started = SharingStarted.WhileSubscribed(SUBSCRIPTION_TIMEOUT_MILLIS),
            initialValue = WelcomeUiState(
                appLanguage = preferences.preferences.value.appLanguage,
                regionCode = preferences.preferences.value.regionCode,
                theme = preferences.preferences.value.theme,
            ),
        )

    /**
     * Act on something the user did.
     *
     * The button says "Start browsing" and it means it: the three screens
     * after it are Meet Taffy, Get started and AI setup, and the only one
     * that asks for anything asks for two things a person may decline. The
     * sequence's last screen is what records that it finished.
     */
    fun onIntent(intent: WelcomeIntent, navigator: TaffyNavigator) {
        when (intent) {
            WelcomeIntent.StartBrowsing -> navigator.goTo(TaffyDestination.MeetTaffy)
            WelcomeIntent.OpenLanguageRegion ->
                navigator.goTo(TaffyDestination.LanguageRegion)
            is WelcomeIntent.ChooseTheme -> viewModelScope.launch {
                preferences.setTheme(intent.theme)
            }
        }
    }

    /** Record that this screen was shown. */
    fun onShown() {
        analytics.record(AnalyticsEvent.ScreenShown(TaffyDestination.OnboardingWelcome.screenId))
    }

    private companion object {
        const val SUBSCRIPTION_TIMEOUT_MILLIS = 5_000L
    }
}

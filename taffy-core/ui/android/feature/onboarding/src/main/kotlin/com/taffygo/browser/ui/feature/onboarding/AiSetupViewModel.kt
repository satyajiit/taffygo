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
import com.taffygo.browser.ui.core.model.ProviderRoute
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyNavigator
import javax.inject.Inject
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.flow.stateIn
import kotlinx.coroutines.launch

/** Screen SCR-004's one source of truth. */
class AiSetupViewModel @Inject constructor(
    private val preferences: UserPreferencesRepository,
    private val analytics: AnalyticsClient,
) : ViewModel() {

    /*
     * Selection is a screen draft until persistence catches up. Updating it
     * synchronously means a rapid route tap followed by Continue cannot read
     * the previous DataStore value.
     */
    private val selectedRoute = MutableStateFlow(
        preferences.preferences.value.providerRoute,
    )
    private val finishing = MutableStateFlow(false)

    /** What screen SCR-004 renders. */
    val state: StateFlow<AiSetupUiState> = combine(
        selectedRoute,
        finishing,
    ) { route, isFinishing ->
        AiSetupUiState(route = route, finishing = isFinishing)
    }
        .stateIn(
            scope = viewModelScope,
            started = SharingStarted.WhileSubscribed(SUBSCRIPTION_TIMEOUT_MILLIS),
            initialValue = AiSetupUiState(route = selectedRoute.value),
        )

    /**
     * Act on something the user did.
     *
     * This is the sequence's last screen, so finishing here is what records
     * that the first run is over. Both finishing intents do that; they differ
     * in what they leave behind them and where they land.
     */
    fun onIntent(intent: AiSetupIntent, navigator: TaffyNavigator) {
        when (intent) {
            is AiSetupIntent.ChooseRoute -> {
                if (finishing.value) return
                selectedRoute.value = intent.route
                viewModelScope.launch {
                    preferences.setProviderRoute(intent.route)
                }
            }
            AiSetupIntent.Finish -> {
                if (finishing.value) return
                val route = selectedRoute.value
                if (route != ProviderRoute.DIRECT_WITH_YOUR_KEY) return
                finishing.value = true
                viewModelScope.launch {
                    preferences.setProviderRoute(route)
                    preferences.setOnboardingCompleted(true)
                    navigator.restart(TaffyDestination.BrowserMain)
                    // The browser stays under provider setup, so Back returns
                    // to a usable product rather than to finished onboarding.
                    navigator.goTo(TaffyDestination.AiAndProviders)
                }
            }
            AiSetupIntent.SetUpLater -> {
                if (finishing.value) return
                finishing.value = true
                viewModelScope.launch {
                    /*
                     * Written rather than assumed. NOT_CONFIGURED is the
                     * default, but it is not necessarily what is stored: a
                     * person can choose the key route, change their mind and
                     * skip, and leaving the earlier write standing would send
                     * them to a browser that believes a provider is coming.
                     */
                    selectedRoute.value = ProviderRoute.NOT_CONFIGURED
                    preferences.setProviderRoute(ProviderRoute.NOT_CONFIGURED)
                    preferences.setOnboardingCompleted(true)
                    navigator.restart(TaffyDestination.BrowserMain)
                }
            }
        }
    }

    /** Record that this screen was shown. */
    fun onShown() {
        analytics.record(AnalyticsEvent.ScreenShown(TaffyDestination.AiSetup.screenId))
    }

    private companion object {
        const val SUBSCRIPTION_TIMEOUT_MILLIS = 5_000L
    }
}

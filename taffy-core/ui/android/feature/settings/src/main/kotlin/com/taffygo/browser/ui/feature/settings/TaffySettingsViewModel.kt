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
import com.taffygo.browser.ui.core.ui.TaffyNavigator
import javax.inject.Inject
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.flow.stateIn
import kotlinx.coroutines.launch

/** Screen SCR-405's one source of truth. */
class TaffySettingsViewModel @Inject constructor(
    private val preferences: UserPreferencesRepository,
    private val analytics: AnalyticsClient,
) : ViewModel() {

    /**
     * What screen SCR-405 renders.
     *
     * Projected straight from the stored preferences, so the switch shows what
     * is saved rather than what was pressed: a write that does not land leaves
     * the switch where it was, which is the truthful drawing of a setting that
     * did not change.
     */
    val state: StateFlow<TaffySettingsUiState> = preferences.preferences
        .map { TaffySettingsUiState(composerSuggestions = it.composerSuggestions) }
        .stateIn(
            scope = viewModelScope,
            started = SharingStarted.WhileSubscribed(SUBSCRIPTION_TIMEOUT_MILLIS),
            initialValue = TaffySettingsUiState(
                composerSuggestions = preferences.preferences.value.composerSuggestions,
            ),
        )

    /** Act on something the user did. */
    fun onIntent(intent: TaffySettingsIntent, navigator: TaffyNavigator) {
        when (intent) {
            TaffySettingsIntent.OpenPersonality -> navigator.goTo(TaffyDestination.Personality)
            TaffySettingsIntent.OpenSkills -> navigator.goTo(TaffyDestination.SkillsList)
            // SCR-419 rather than the hub: the row means "my AI providers",
            // and somebody who has set some up came to see those. SCR-419
            // hands straight over to the hub when there are none, so a first
            // run still lands on the screen that can add one.
            TaffySettingsIntent.OpenAiProviders ->
                navigator.goTo(TaffyDestination.ConnectedProviders)
            TaffySettingsIntent.ToggleComposerSuggestions -> toggleComposerSuggestions()
        }
    }

    /** Record that this screen was shown. */
    fun onShown() {
        analytics.record(AnalyticsEvent.ScreenShown(TaffyDestination.TaffySettings.screenId))
    }

    private fun toggleComposerSuggestions() {
        val wanted = !state.value.composerSuggestions
        viewModelScope.launch { preferences.setComposerSuggestions(wanted) }
    }

    private companion object {
        const val SUBSCRIPTION_TIMEOUT_MILLIS = 5_000L
    }
}

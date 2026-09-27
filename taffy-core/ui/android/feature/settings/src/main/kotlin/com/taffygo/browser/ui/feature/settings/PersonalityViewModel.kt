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
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyNavigator
import javax.inject.Inject
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.flow.stateIn
import kotlinx.coroutines.launch

/** Screen SCR-603's one source of truth. */
class PersonalityViewModel @Inject constructor(
    private val personality: PersonalityRepository,
    private val analytics: AnalyticsClient,
) : ViewModel() {

    private val choiceNotSaved = MutableStateFlow(false)

    /** What screen SCR-603 renders. */
    val state: StateFlow<PersonalityUiState> =
        combine(personality.snapshot, choiceNotSaved) { snapshot, notSaved ->
            projectPersonality(snapshot, notSaved)
        }.stateIn(
            scope = viewModelScope,
            started = SharingStarted.WhileSubscribed(SUBSCRIPTION_TIMEOUT_MILLIS),
            initialValue = projectPersonality(personality.snapshot.value, choiceNotSaved.value),
        )

    /** Act on something the user did. */
    fun onIntent(intent: PersonalityIntent, navigator: TaffyNavigator) {
        reducePersonality(state.value, intent)
        when (intent) {
            is PersonalityIntent.ChoosePreset -> viewModelScope.launch {
                if (personality.snapshot.value.availability !=
                    PersonalityRepository.Availability.READY
                ) {
                    choiceNotSaved.value = true
                    return@launch
                }
                personality.choosePreset(intent.preset)
            }
            PersonalityIntent.OpenTuning ->
                navigator.goTo(TaffyDestination.PersonalityTuning)
        }
    }

    /** Record that this screen was shown. */
    fun onShown() {
        analytics.record(AnalyticsEvent.ScreenShown(TaffyDestination.Personality.screenId))
    }

    private companion object {
        const val SUBSCRIPTION_TIMEOUT_MILLIS = 5_000L
    }
}

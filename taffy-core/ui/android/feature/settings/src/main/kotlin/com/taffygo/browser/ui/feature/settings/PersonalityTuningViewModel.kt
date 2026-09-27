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
import javax.inject.Inject
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.flow.stateIn
import kotlinx.coroutines.launch

/** Screen SCR-604's one source of truth. */
class PersonalityTuningViewModel @Inject constructor(
    private val personality: PersonalityRepository,
    private val analytics: AnalyticsClient,
) : ViewModel() {

    private val notSaved = MutableStateFlow(false)

    /** What screen SCR-604 renders. */
    val state: StateFlow<PersonalityTuningUiState> =
        combine(personality.snapshot, notSaved) { snapshot, unsaved ->
            projectPersonalityTuning(snapshot, unsaved)
        }.stateIn(
            scope = viewModelScope,
            started = SharingStarted.WhileSubscribed(SUBSCRIPTION_TIMEOUT_MILLIS),
            initialValue = projectPersonalityTuning(personality.snapshot.value, notSaved.value),
        )

    /** Act on something the user did. */
    fun onIntent(intent: PersonalityTuningIntent) {
        reducePersonalityTuning(state.value, intent)
        when (intent) {
            is PersonalityTuningIntent.SetScale -> viewModelScope.launch {
                if (personality.snapshot.value.availability !=
                    PersonalityRepository.Availability.READY
                ) {
                    notSaved.value = true
                    return@launch
                }
                val next = applyTuningScale(
                    personality.snapshot.value.scales,
                    intent.axis,
                    intent.value,
                )
                personality.setScales(next)
            }
        }
    }

    /** Record that this screen was shown. */
    fun onShown() {
        analytics.record(
            AnalyticsEvent.ScreenShown(TaffyDestination.PersonalityTuning.screenId),
        )
    }

    private companion object {
        const val SUBSCRIPTION_TIMEOUT_MILLIS = 5_000L
    }
}

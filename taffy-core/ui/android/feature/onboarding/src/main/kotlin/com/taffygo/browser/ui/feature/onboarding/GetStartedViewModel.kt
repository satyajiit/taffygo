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
import com.taffygo.browser.ui.core.preferences.LocalProfileRepository
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyNavigator
import javax.inject.Inject
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch

/**
 * Screen SCR-007's one source of truth.
 *
 * The profile is not a port this screen reads from. It reads nothing: a first
 * run has no name and no picture yet, and seeding the field from an empty
 * store would only be a way to get it wrong. It writes once, on the way out.
 */
class GetStartedViewModel @Inject constructor(
    private val profile: LocalProfileRepository,
    private val analytics: AnalyticsClient,
) : ViewModel() {

    private val local = MutableStateFlow(GetStartedUiState())

    /** What screen SCR-007 renders. */
    val state: StateFlow<GetStartedUiState> = local.asStateFlow()

    /** Act on something the user did. */
    fun onIntent(intent: GetStartedIntent, navigator: TaffyNavigator) {
        if (local.value.continuing) return
        local.value = reduceGetStarted(local.value, intent)
        when (intent) {
            GetStartedIntent.Continue -> finish(navigator)
            is GetStartedIntent.EditName,
            is GetStartedIntent.ChooseAvatar,
            GetStartedIntent.OpenDataSheet,
            GetStartedIntent.CloseDataSheet,
            -> Unit
        }
    }

    /**
     * Write both answers, then go on.
     *
     * Both are written even when neither was changed, and deliberately: the
     * monogram has no stored id, so "chose the monogram" and "never answered"
     * are the same absent value, and writing settles it. The write happens
     * before the navigation rather than beside it, so AI setup cannot be
     * reached by a profile that has not been stored yet.
     */
    private fun finish(navigator: TaffyNavigator) {
        val answered = local.value
        viewModelScope.launch {
            profile.setDisplayName(getStartedStoredName(answered))
            profile.setAvatar(answered.avatar)
            navigator.goTo(TaffyDestination.AiSetup)
        }
    }

    /** Record that this screen was shown. */
    fun onShown() {
        analytics.record(AnalyticsEvent.ScreenShown(TaffyDestination.GetStarted.screenId))
    }
}

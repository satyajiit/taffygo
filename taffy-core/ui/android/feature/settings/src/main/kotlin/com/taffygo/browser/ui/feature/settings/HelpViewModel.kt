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
import com.taffygo.browser.ui.core.browser.BrowserRepository
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyNavigator
import com.taffygo.browser.ui.core.ui.TaffyProjectContact
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch

/**
 * Screen SCR-409's one source of truth.
 *
 * TaffyGo runs no server (decision 0200), so feedback has two routes and both
 * leave through something the person controls: an email draft their own app
 * opens, or GitHub's new-issue page in a tab. The draft is handed over by the
 * screen, because starting another app needs a context; the issue page is
 * opened here the way About opens the source.
 */
class HelpViewModel(
    private val browser: BrowserRepository,
    private val analytics: AnalyticsClient,
) : ViewModel() {
    private val presentation = MutableStateFlow(HelpUiState())

    val state: StateFlow<HelpUiState> = presentation.asStateFlow()

    fun onIntent(intent: HelpIntent, navigator: TaffyNavigator) {
        presentation.value = reduceHelp(presentation.value, intent)
        when (intent) {
            HelpIntent.Dismiss -> navigator.goBack()
            HelpIntent.OpenPublicIssue -> viewModelScope.launch {
                if (browser.openTab(TaffyProjectContact.ISSUE_CHOOSER).value.isNotEmpty()) {
                    navigator.goTo(TaffyDestination.BrowserMain)
                }
            }
            is HelpIntent.FeedbackByEmail -> Unit
        }
    }

    fun onShown() {
        analytics.record(AnalyticsEvent.ScreenShown(TaffyDestination.HelpAndFeedback.screenId))
    }
}

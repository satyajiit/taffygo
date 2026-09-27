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
 * Screen SCR-408's one source of truth.
 *
 * There is no reducer beneath it. Both facts are read once at construction and
 * neither can change while the window lives, so every intent here is a
 * navigation and the state is a constant. Two of those navigations open a page:
 * the notices and the source each open in a new tab, and the browser is shown
 * only once a tab exists to show.
 */
class AboutViewModel(
    about: AboutRepository,
    private val browser: BrowserRepository,
    private val analytics: AnalyticsClient,
) : ViewModel() {
    private val presentation = MutableStateFlow(AboutUiState(facts = about.facts))

    val state: StateFlow<AboutUiState> = presentation.asStateFlow()

    fun onIntent(intent: AboutIntent, navigator: TaffyNavigator) {
        when (intent) {
            AboutIntent.OpenHelp -> navigator.goTo(TaffyDestination.HelpAndFeedback)
            AboutIntent.OpenLicences -> viewModelScope.launch {
                if (browser.openAttributionNotice()) navigator.goTo(TaffyDestination.BrowserMain)
            }
            // The public repository (decisions 0206 and 0251): an ordinary https address,
            // opened the way every other address is, and written once beside the address a
            // report goes to. The licences row is the other half of decision 0206 and takes
            // no address at all: see BrowserRepository.openAttributionNotice.
            AboutIntent.OpenSource -> viewModelScope.launch {
                if (browser.openTab(TaffyProjectContact.REPOSITORY).value.isNotEmpty()) {
                    navigator.goTo(TaffyDestination.BrowserMain)
                }
            }
            AboutIntent.Dismiss -> navigator.goBack()
        }
    }

    fun onShown() {
        analytics.record(AnalyticsEvent.ScreenShown(TaffyDestination.About.screenId))
    }
}

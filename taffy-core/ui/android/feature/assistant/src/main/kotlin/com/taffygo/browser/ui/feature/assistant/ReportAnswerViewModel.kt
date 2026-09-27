// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.assistant

import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.taffygo.browser.ui.core.api.CoreApiClient
import com.taffygo.browser.ui.core.browser.BrowserRepository
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyNavigator
import com.taffygo.browser.ui.core.ui.TaffyProjectContact
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch

/**
 * The report sheet's one source of truth, beside whichever surface shows an
 * answer: the Ask overlay's conversation and screen SCR-303's answer panel.
 *
 * Nothing here sends anything. The email draft is handed to the person's own
 * app by the sheet, because starting another app needs a context; this reads
 * the providers once when the sheet opens, and opens the public issue form in
 * a tab the way About opens the source.
 */
class ReportAnswerViewModel(
    private val core: CoreApiClient,
    private val browser: BrowserRepository,
) : ViewModel() {
    private val presentation = MutableStateFlow(ReportAnswerUiState())

    val state: StateFlow<ReportAnswerUiState> = presentation.asStateFlow()

    fun onIntent(intent: ReportAnswerIntent, navigator: TaffyNavigator) {
        val providers = if (intent is ReportAnswerIntent.Open) {
            reportedProviders(core.status.value)
        } else {
            emptyList()
        }
        presentation.value = reduceReportAnswer(presentation.value, intent, providers)
        if (intent is ReportAnswerIntent.OpenPublicIssue) {
            viewModelScope.launch {
                val address = TaffyProjectContact.newIssueAddress(intent.title)
                if (browser.openTab(address).value.isNotEmpty()) {
                    navigator.goTo(TaffyDestination.BrowserMain)
                }
            }
        }
    }
}

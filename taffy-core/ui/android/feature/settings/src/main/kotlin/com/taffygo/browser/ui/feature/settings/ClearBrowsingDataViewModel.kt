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
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch

/** Screen SCR-207's one source of truth. */
class ClearBrowsingDataViewModel(
    private val clearData: ClearDataRepository,
    private val analytics: AnalyticsClient,
) : ViewModel() {

    private val internalState = MutableStateFlow(
        ClearBrowsingDataUiState(
            available = clearData.available,
            classes = clearData.supportedClasses,
            supportedClasses = clearData.supportedClasses,
        ),
    )

    val state: StateFlow<ClearBrowsingDataUiState> = internalState.asStateFlow()

    fun onIntent(intent: ClearBrowsingDataIntent, navigator: TaffyNavigator) {
        val current = internalState.value
        val reduced = reduceClearBrowsingData(current, intent)
        internalState.value = reduced
        when (intent) {
            ClearBrowsingDataIntent.Dismiss -> navigator.goBack()
            ClearBrowsingDataIntent.Submit -> if (!current.submitting && reduced.submitting) {
                viewModelScope.launch { submit(navigator) }
            }
            else -> Unit
        }
    }

    private suspend fun submit(navigator: TaffyNavigator) {
        val current = internalState.value
        if (!current.available) {
            internalState.value = current.copy(submitting = false, confirming = false)
            return
        }
        val cleared = clearData.clear(current.range, current.classes)
        internalState.value = current.copy(
            submitting = false,
            confirming = false,
            failed = !cleared,
        )
        if (cleared) navigator.goBack()
    }

    fun onShown() {
        analytics.record(AnalyticsEvent.ScreenShown(TaffyDestination.ClearBrowsingData.screenId))
    }
}

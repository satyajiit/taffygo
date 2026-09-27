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
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.flow.stateIn

/** Screen SCR-412's one source of truth. */
class WhatHappenedViewModel(
    repository: WhatHappenedRepository,
    private val analytics: AnalyticsClient,
) : ViewModel() {
    /** What screen SCR-412 renders. */
    val state: StateFlow<WhatHappenedUiState> =
        repository.snapshot.map(::projectWhatHappened)
            .stateIn(
                scope = viewModelScope,
                started = SharingStarted.WhileSubscribed(SUBSCRIPTION_TIMEOUT_MILLIS),
                initialValue = projectWhatHappened(repository.snapshot.value),
            )

    /** Act on something the user did. */
    fun onIntent(intent: WhatHappenedIntent, navigator: TaffyNavigator) {
        if (intent is WhatHappenedIntent.OpenTask) {
            val event = state.value.days.asSequence()
                .flatMap { it.events }
                .firstOrNull { it.workspaceId == intent.workspaceId }
            if (event != null && !event.workspaceGone) {
                navigator.goTo(TaffyDestination.WorkspaceDetail(intent.workspaceId))
            }
        }
    }

    /** Record that this screen was shown. */
    fun onShown() {
        analytics.record(AnalyticsEvent.ScreenShown(TaffyDestination.WhatHappened.screenId))
    }

    private companion object {
        const val SUBSCRIPTION_TIMEOUT_MILLIS = 5_000L
    }
}

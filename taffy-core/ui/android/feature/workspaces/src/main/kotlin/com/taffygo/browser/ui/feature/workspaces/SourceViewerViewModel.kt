// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.workspaces

import androidx.lifecycle.SavedStateHandle
import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.taffygo.browser.ui.core.analytics.AnalyticsClient
import com.taffygo.browser.ui.core.analytics.AnalyticsEvent
import com.taffygo.browser.ui.core.browser.BrowserRepository
import com.taffygo.browser.ui.core.workspace.WorkspaceRepository
import com.taffygo.browser.ui.core.model.SourceId
import com.taffygo.browser.ui.core.model.WorkspaceId
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyNavigator
import javax.inject.Inject
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.flow.stateIn
import kotlinx.coroutines.launch

/** Screen SCR-306's one source of truth. */
class SourceViewerViewModel @Inject constructor(
    private val workspaces: WorkspaceRepository,
    private val browser: BrowserRepository,
    private val analytics: AnalyticsClient,
    savedState: SavedStateHandle,
) : ViewModel() {

    private val workspaceId = WorkspaceId(savedState.get<String>(TaffyDestination.WORKSPACE_ID).orEmpty())
    private val sourceId = SourceId(savedState.get<String>(TaffyDestination.SOURCE_ID).orEmpty())

    /** What screen SCR-306 renders. */
    val state: StateFlow<SourceViewerUiState> = combine(
        workspaces.availability,
        workspaces.workspaces,
    ) { availability, all ->
        projectSourceViewer(
            workspace = all.firstOrNull { it.id == workspaceId },
            sourceId = sourceId,
            loading = availability == WorkspaceRepository.Availability.LOADING,
            unavailable = availability == WorkspaceRepository.Availability.UNAVAILABLE,
        )
    }
        .stateIn(
            scope = viewModelScope,
            started = SharingStarted.WhileSubscribed(SUBSCRIPTION_TIMEOUT_MILLIS),
            initialValue = projectSourceViewer(
                workspaces.workspace(workspaceId),
                sourceId,
                loading = workspaces.availability.value == WorkspaceRepository.Availability.LOADING,
                unavailable = workspaces.availability.value ==
                    WorkspaceRepository.Availability.UNAVAILABLE,
            ),
        )

    /** Act on something the user did. */
    fun onIntent(intent: SourceViewerIntent, navigator: TaffyNavigator) {
        when (intent) {
            SourceViewerIntent.OpenLivePage -> viewModelScope.launch {
                browser.openTab(state.value.host)
                navigator.goTo(TaffyDestination.BrowserMain)
            }
            SourceViewerIntent.Close -> navigator.goBack()
        }
    }

    /** Record that this screen was shown. */
    fun onShown() {
        analytics.record(
            AnalyticsEvent.ScreenShown(
                TaffyDestination.SourceViewer(workspaceId.value, sourceId.value).screenId,
            ),
        )
    }

    private companion object {
        const val SUBSCRIPTION_TIMEOUT_MILLIS = 5_000L
    }
}

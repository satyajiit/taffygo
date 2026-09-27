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
import com.taffygo.browser.ui.core.browser.BrowserProfilesRepository
import com.taffygo.browser.ui.core.model.TaskTemplate
import com.taffygo.browser.ui.core.model.Workspace
import com.taffygo.browser.ui.core.workspace.WorkspaceRepository
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyNavigator
import javax.inject.Inject
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.flow.stateIn

/** Screen SCR-304's one source of truth. */
class WorkspaceListViewModel @Inject constructor(
    private val workspaces: WorkspaceRepository,
    private val profiles: BrowserProfilesRepository,
    private val analytics: AnalyticsClient,
    private val savedState: SavedStateHandle,
) : ViewModel() {

    private val query = MutableStateFlow(savedState.get<String>(QUERY_KEY).orEmpty())

    /** What screen SCR-304 renders. */
    val state: StateFlow<WorkspaceListUiState> =
        combine(
            workspaces.availability,
            workspaces.workspaces,
            query,
            profiles.snapshot,
            ::project,
        )
            .stateIn(
                scope = viewModelScope,
                started = SharingStarted.WhileSubscribed(SUBSCRIPTION_TIMEOUT_MILLIS),
                initialValue = project(
                    workspaces.availability.value,
                    workspaces.workspaces.value,
                    query.value,
                    profiles.snapshot.value,
                ),
            )

    /** Act on something the user did. */
    fun onIntent(intent: WorkspaceListIntent, navigator: TaffyNavigator) {
        when (intent) {
            is WorkspaceListIntent.QueryChanged -> {
                query.value = intent.query
                savedState[QUERY_KEY] = intent.query
            }
            is WorkspaceListIntent.Open ->
                navigator.goTo(TaffyDestination.WorkspaceDetail(intent.id.value))
            // A new workspace is a source table not yet built: the Ask sheet
            // with that shape stated, where the page it reads is named.
            WorkspaceListIntent.Create -> navigator.goTo(
                TaffyDestination.AssistantBar(shape = TaskTemplate.BUILD_A_SOURCE_TABLE),
            )
        }
    }

    /**
     * Record that this screen was shown, and read the profiles so the list can
     * say which one it belongs to (decision 0102).
     */
    fun onShown() {
        analytics.record(AnalyticsEvent.ScreenShown(TaffyDestination.WorkspaceList.screenId))
        profiles.refresh()
    }

    private fun project(
        availability: WorkspaceRepository.Availability,
        list: List<Workspace>,
        query: String,
        profileSnapshot: BrowserProfilesRepository.Snapshot,
    ): WorkspaceListUiState = projectWorkspaceList(
        workspaces = list,
        query = query,
        loading = availability == WorkspaceRepository.Availability.LOADING,
        unavailable = availability == WorkspaceRepository.Availability.UNAVAILABLE,
        profileName = workspaceProfileName(profileSnapshot),
    )

    private companion object {
        const val SUBSCRIPTION_TIMEOUT_MILLIS = 5_000L
        const val QUERY_KEY = "workspace_list_query"
    }
}

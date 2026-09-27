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
import com.taffygo.browser.ui.core.workspace.WorkspaceRepository
import com.taffygo.browser.ui.core.model.WorkspaceId
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyNavigator
import javax.inject.Inject
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.flow.stateIn
import kotlinx.coroutines.launch

/**
 * Screen SCR-305's one source of truth.
 *
 * The workspace identifier is a navigation argument, so it arrives through the
 * saved-state handle and survives process death without the screen having to
 * remember it.
 */
class WorkspaceDetailViewModel @Inject constructor(
    private val workspaces: WorkspaceRepository,
    private val library: LibraryRepository,
    private val analytics: AnalyticsClient,
    savedState: SavedStateHandle,
) : ViewModel() {

    private val workspaceId = WorkspaceId(savedState.get<String>(WORKSPACE_ID_KEY).orEmpty())

    /** What screen SCR-305 renders. */
    val state: StateFlow<WorkspaceDetailUiState> = combine(
        workspaces.availability,
        workspaces.workspaces,
        library.snapshot,
    ) { availability, all, _ ->
        projectWorkspaceDetail(
            all.firstOrNull { it.id == workspaceId },
            loading = availability == WorkspaceRepository.Availability.LOADING,
            unavailable = availability == WorkspaceRepository.Availability.UNAVAILABLE,
            canKeepFacts = library.canKeep,
        )
    }
        .stateIn(
            scope = viewModelScope,
            started = SharingStarted.WhileSubscribed(SUBSCRIPTION_TIMEOUT_MILLIS),
            initialValue = projectWorkspaceDetail(
                workspaces.workspace(workspaceId),
                loading = workspaces.availability.value == WorkspaceRepository.Availability.LOADING,
                unavailable = workspaces.availability.value ==
                    WorkspaceRepository.Availability.UNAVAILABLE,
                canKeepFacts = library.canKeep,
            ),
        )

    /** Act on something the user did. */
    fun onIntent(intent: WorkspaceDetailIntent, navigator: TaffyNavigator) {
        when (intent) {
            is WorkspaceDetailIntent.CorrectFact -> navigator.goTo(
                TaffyDestination.FactCorrection(workspaceId.value, intent.factId.value),
            )
            is WorkspaceDetailIntent.OpenSource -> navigator.goTo(
                TaffyDestination.SourceViewer(workspaceId.value, intent.sourceId.value),
            )
            is WorkspaceDetailIntent.ExcludeSource -> viewModelScope.launch {
                workspaces.excludeSource(workspaceId, intent.sourceId)
            }
            is WorkspaceDetailIntent.KeepFact -> viewModelScope.launch {
                val current = state.value
                val id = current.id ?: return@launch
                library.saveFact(id.value, current.revision, intent.factId.value)
            }
            WorkspaceDetailIntent.Export ->
                navigator.goTo(TaffyDestination.ExportSheet(workspaceId.value))
            is WorkspaceDetailIntent.Rename -> viewModelScope.launch {
                workspaces.rename(workspaceId, intent.expectedRevision, intent.displayName)
            }
            is WorkspaceDetailIntent.Delete -> viewModelScope.launch {
                workspaces.delete(
                    workspaceId,
                    intent.expectedRevision,
                    intent.confirmationToken,
                )
            }
        }
    }

    /** Record that this screen was shown. */
    fun onShown() {
        analytics.record(
            AnalyticsEvent.ScreenShown(TaffyDestination.WorkspaceDetail(workspaceId.value).screenId),
        )
    }

    private companion object {
        const val SUBSCRIPTION_TIMEOUT_MILLIS = 5_000L

        /** The navigation argument the shell puts into the saved state. */
        val WORKSPACE_ID_KEY = TaffyDestination.WORKSPACE_ID
    }
}

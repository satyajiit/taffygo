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
import com.taffygo.browser.ui.core.common.TaffyResult
import com.taffygo.browser.ui.core.workspace.WorkspaceRepository
import com.taffygo.browser.ui.core.model.FactId
import com.taffygo.browser.ui.core.model.WorkspaceId
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyNavigator
import javax.inject.Inject
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.launch

/** Screen SCR-307's one source of truth. */
class FactCorrectionViewModel @Inject constructor(
    private val workspaces: WorkspaceRepository,
    private val analytics: AnalyticsClient,
    savedState: SavedStateHandle,
) : ViewModel() {

    private val workspaceId = WorkspaceId(savedState.get<String>(TaffyDestination.WORKSPACE_ID).orEmpty())
    private val factId = FactId(savedState.get<String>(TaffyDestination.FACT_ID).orEmpty())

    private val internalState = MutableStateFlow(
        projectFactCorrection(
            workspaces.workspace(workspaceId),
            factId,
            loading = workspaces.availability.value != WorkspaceRepository.Availability.UNAVAILABLE &&
                workspaces.workspace(workspaceId) == null,
            unavailable = workspaces.availability.value ==
                WorkspaceRepository.Availability.UNAVAILABLE,
        ),
    )

    /** What screen SCR-307 renders. */
    val state: StateFlow<FactCorrectionUiState> = internalState.asStateFlow()

    init {
        viewModelScope.launch {
            combine(
                workspaces.availability,
                workspaces.workspaces,
                ::Pair,
            ).collect { (availability, all) ->
                when {
                    availability == WorkspaceRepository.Availability.LOADING ->
                        internalState.value = FactCorrectionUiState(loading = true)
                    availability == WorkspaceRepository.Availability.UNAVAILABLE ->
                        internalState.value = FactCorrectionUiState(unavailable = true)
                    internalState.value.loading || internalState.value.unavailable ->
                        internalState.value = projectFactCorrection(
                            all.firstOrNull { it.id == workspaceId },
                            factId,
                        )
                }
            }
        }
    }

    /** Act on something the user did. */
    fun onIntent(intent: FactCorrectionIntent, navigator: TaffyNavigator) {
        internalState.value = reduceFactCorrection(internalState.value, intent)

        when (intent) {
            FactCorrectionIntent.Save -> if (internalState.value.canSave) {
                val entered = internalState.value.enteredValue
                viewModelScope.launch {
                    if (workspaces.correctFact(workspaceId, factId, entered) is TaffyResult.Success) {
                        navigator.goBack()
                    }
                }
            }
            FactCorrectionIntent.Cancel -> navigator.goBack()
            is FactCorrectionIntent.ValueChanged -> Unit
        }
    }

    /** Record that this screen was shown. */
    fun onShown() {
        analytics.record(
            AnalyticsEvent.ScreenShown(
                TaffyDestination.FactCorrection(workspaceId.value, factId.value).screenId,
            ),
        )
    }
}

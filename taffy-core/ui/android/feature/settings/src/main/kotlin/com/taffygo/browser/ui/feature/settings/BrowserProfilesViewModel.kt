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
import com.taffygo.browser.ui.core.browser.BrowserProfilesRepository
import com.taffygo.browser.ui.core.model.Workspace
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.workspace.WorkspaceRepository
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.flow.stateIn
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch

/** Screen SCR-708's one source of truth. */
class BrowserProfilesViewModel(
    private val repository: BrowserProfilesRepository,
    private val workspaces: WorkspaceRepository,
    private val analytics: AnalyticsClient,
) : ViewModel() {
    private val local = MutableStateFlow(BrowserProfilesUiState())

    /** The current profile's workspace count, once the list is known (decision 0102). */
    private val activeWorkspaceCount = combine(
        workspaces.availability,
        workspaces.workspaces,
        ::countWhenReady,
    )

    val state: StateFlow<BrowserProfilesUiState> = combine(
        repository.snapshot,
        local,
        activeWorkspaceCount,
        ::projectBrowserProfiles,
    ).stateIn(
        scope = viewModelScope,
        started = SharingStarted.WhileSubscribed(SUBSCRIPTION_TIMEOUT_MILLIS),
        initialValue = projectBrowserProfiles(
            repository.snapshot.value,
            local.value,
            countWhenReady(workspaces.availability.value, workspaces.workspaces.value),
        ),
    )

    fun onShown() {
        analytics.record(AnalyticsEvent.ScreenShown(TaffyDestination.BrowserProfiles.screenId))
        repository.refresh()
    }

    fun onIntent(intent: BrowserProfilesIntent) {
        if (intent is BrowserProfilesIntent.ConfirmDelete) {
            delete()
            return
        }
        // The reducer reads facts only the projection carries — availability,
        // the profile list — so it must run against the projected state, not
        // the local slice, which starts LOADING and empty for the whole
        // lifetime of the screen. Reducing the local slice left "Delete" a
        // no-op on a phone (verification report section 2.8). Only the local
        // fields are written back; the projection re-derives the rest on the
        // way out.
        val reduced = reduceBrowserProfiles(current(), intent)
        local.update {
            it.copy(
                deleteCandidate = reduced.deleteCandidate,
                failure = reduced.failure,
            )
        }
        if (intent == BrowserProfilesIntent.Refresh) repository.refresh()
    }

    private fun delete() {
        val projected = current()
        val profileId = projected.deleteCandidate?.id ?: return
        if (projected.busy) return
        local.update { it.copy(operation = BrowserProfilesUiState.Operation.DELETING, failure = null) }
        viewModelScope.launch {
            val result = repository.delete(profileId)
            local.update {
                it.copy(
                    operation = null,
                    deleteCandidate = if (result.succeeded) null else it.deleteCandidate,
                    failure = result.failure,
                )
            }
            if (result.succeeded) repository.refresh()
        }
    }

    /** The projection as a subscriber would see it now, whether or not one is attached. */
    private fun current(): BrowserProfilesUiState = projectBrowserProfiles(
        repository.snapshot.value,
        local.value,
        countWhenReady(workspaces.availability.value, workspaces.workspaces.value),
    )

    private fun countWhenReady(
        availability: WorkspaceRepository.Availability,
        list: List<Workspace>,
    ): Int? = list.size.takeIf { availability == WorkspaceRepository.Availability.READY }

    private companion object {
        const val SUBSCRIPTION_TIMEOUT_MILLIS = 5_000L
    }
}

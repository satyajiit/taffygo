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
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.flow.stateIn
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch

/** Screen SCR-414's one source of truth. */
class SavedDetailsViewModel(
    private val repository: SavedDetailsRepository,
    private val analytics: AnalyticsClient,
) : ViewModel() {

    private val local = MutableStateFlow(Local())

    /** What screen SCR-414 renders. */
    val state: StateFlow<SavedDetailsUiState> =
        combine(repository.snapshot, local) { snapshot, screen ->
            projectSavedDetails(snapshot, screen.editor, screen.confirmDelete)
        }.stateIn(
            scope = viewModelScope,
            started = SharingStarted.WhileSubscribed(SUBSCRIPTION_TIMEOUT_MILLIS),
            initialValue = projectSavedDetails(
                repository.snapshot.value,
                local.value.editor,
                local.value.confirmDelete,
            ),
        )

    /** Act on something the user did. */
    fun onIntent(intent: SavedDetailsIntent) {
        when (intent) {
            SavedDetailsIntent.Save -> {
                val editor = local.value.editor ?: return
                viewModelScope.launch {
                    repository.upsert(editor.toPerson())
                    local.value = Local()
                }
            }
            SavedDetailsIntent.ConfirmDelete -> {
                val id = local.value.editor?.id ?: return
                viewModelScope.launch {
                    repository.delete(id)
                    local.value = Local()
                }
            }
            else -> local.update { reduceSavedDetails(state.value, intent).let { next ->
                Local(editor = next.editor, confirmDelete = next.confirmDelete)
            } }
        }
    }

    /** Record that this screen was shown. */
    fun onShown() {
        analytics.record(AnalyticsEvent.ScreenShown(TaffyDestination.SavedDetails.screenId))
    }

    private data class Local(
        val editor: SavedDetailsUiState.Editor? = null,
        val confirmDelete: Boolean = false,
    )

    private companion object {
        const val SUBSCRIPTION_TIMEOUT_MILLIS = 5_000L
    }
}

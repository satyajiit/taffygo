// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import androidx.lifecycle.SavedStateHandle
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

/** Screen SCR-413's metadata-only source of truth. */
class SavedSignInsViewModel(
    private val repository: SavedSignInsRepository,
    private val analytics: AnalyticsClient,
    private val savedState: SavedStateHandle,
) : ViewModel() {

    private val local = MutableStateFlow(
        Local(
            query = savedState.get<String>(QUERY_KEY).orEmpty(),
            openedId = savedState.get<String>(OPENED_KEY),
        ),
    )

    val state: StateFlow<SavedSignInsUiState> =
        combine(repository.snapshot, local) { snapshot, screen ->
            projectSavedSignIns(
                snapshot = snapshot,
                query = screen.query,
                openedId = screen.openedId,
                confirmDelete = screen.confirmDelete,
            )
        }.stateIn(
            scope = viewModelScope,
            started = SharingStarted.WhileSubscribed(SUBSCRIPTION_TIMEOUT_MILLIS),
            initialValue = projectSavedSignIns(
                snapshot = repository.snapshot.value,
                query = local.value.query,
                openedId = local.value.openedId,
                confirmDelete = local.value.confirmDelete,
            ),
        )

    fun onIntent(intent: SavedSignInsIntent) {
        when (intent) {
            is SavedSignInsIntent.QueryChanged -> {
                local.update { it.copy(query = intent.query) }
                savedState[QUERY_KEY] = intent.query
            }
            is SavedSignInsIntent.Open -> {
                local.update { it.copy(openedId = intent.id, confirmDelete = false) }
                savedState[OPENED_KEY] = intent.id
            }
            SavedSignInsIntent.DismissDetail -> {
                local.update { it.copy(openedId = null, confirmDelete = false) }
                savedState[OPENED_KEY] = null
            }
            SavedSignInsIntent.Delete ->
                local.update { it.copy(confirmDelete = true) }
            SavedSignInsIntent.ConfirmDelete -> viewModelScope.launch {
                val id = local.value.openedId ?: return@launch
                repository.delete(id)
                local.value = local.value.copy(openedId = null, confirmDelete = false)
                savedState[OPENED_KEY] = null
            }
            SavedSignInsIntent.CancelDelete ->
                local.update { it.copy(confirmDelete = false) }
        }
    }

    fun onShown() {
        analytics.record(AnalyticsEvent.ScreenShown(TaffyDestination.SavedSignIns.screenId))
    }

    private data class Local(
        val query: String,
        val openedId: String?,
        val confirmDelete: Boolean = false,
    )

    private companion object {
        const val QUERY_KEY = "saved_sign_ins_query"
        const val OPENED_KEY = "saved_sign_ins_opened"
        const val SUBSCRIPTION_TIMEOUT_MILLIS = 5_000L
    }
}

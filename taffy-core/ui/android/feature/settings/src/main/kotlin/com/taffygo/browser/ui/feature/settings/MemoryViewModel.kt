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
import com.taffygo.browser.ui.core.common.TaffyResult
import com.taffygo.browser.ui.core.ui.TaffyDestination
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.flow.distinctUntilChanged
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.flow.stateIn
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch

/** Screen SCR-505's one source of truth. */
class MemoryViewModel(
    private val repository: MemoryRepository,
    private val analytics: AnalyticsClient,
    private val savedState: SavedStateHandle,
) : ViewModel() {

    private val local = MutableStateFlow(
        Local(query = savedState.get<String>(QUERY_KEY).orEmpty()),
    )
    private var searchJob: Job? = null

    init {
        requestSearch(local.value.query)
    }

    private val listProjection = combine(
        repository.snapshot,
        local.map { it.query }.distinctUntilChanged(),
    ) { snapshot, query ->
        projectMemory(snapshot, query, editor = null, confirmDelete = false)
    }.stateIn(
        scope = viewModelScope,
        started = SharingStarted.Eagerly,
        initialValue = projectMemory(
            repository.snapshot.value,
            local.value.query,
            editor = null,
            confirmDelete = false,
        ),
    )

    /** What screen SCR-505 renders. */
    val state: StateFlow<MemoryUiState> = combine(listProjection, local) { projected, screen ->
        projected.copy(
            query = screen.query,
            editor = screen.editor,
            confirmDelete = screen.confirmDelete && screen.editor != null,
            mutationInFlight = screen.mutationInFlight,
            mutationFailure = screen.mutationFailure,
        )
    }.stateIn(
        scope = viewModelScope,
        started = SharingStarted.WhileSubscribed(SUBSCRIPTION_TIMEOUT_MILLIS),
        initialValue = listProjection.value.copy(
            editor = local.value.editor,
            confirmDelete = local.value.confirmDelete,
        ),
    )

    /** Act on something the user did. */
    fun onIntent(intent: MemoryIntent) {
        when (intent) {
            is MemoryIntent.QueryChanged -> {
                local.update { it.copy(query = intent.query, mutationFailure = null) }
                savedState[QUERY_KEY] = intent.query
                requestSearch(intent.query)
            }
            MemoryIntent.Save -> {
                val editor = local.value.editor ?: return
                if (local.value.mutationInFlight) return
                val text = editor.text.trim()
                if (text.isEmpty()) return
                local.update { it.copy(mutationInFlight = true, mutationFailure = null) }
                viewModelScope.launch {
                    val result = repository.upsertYouWrote(
                        id = editor.id,
                        statement = text,
                        expectedMemoryRevision = editor.expectedMemoryRevision,
                        expectedRecordRevision = editor.expectedRecordRevision,
                    )
                    settleMutation(editor, MemoryUiState.MutationOperation.SAVE, result)
                }
            }
            MemoryIntent.ConfirmDelete -> {
                val editor = local.value.editor ?: return
                val id = editor.id ?: return
                if (local.value.mutationInFlight) return
                local.update { it.copy(mutationInFlight = true, mutationFailure = null) }
                viewModelScope.launch {
                    val result = repository.delete(
                        id = id,
                        expectedMemoryRevision = editor.expectedMemoryRevision,
                        expectedRecordRevision = editor.expectedRecordRevision,
                    )
                    settleMutation(editor, MemoryUiState.MutationOperation.DELETE, result)
                }
            }
            else -> local.update {
                val next = reduceMemory(
                    listProjection.value.copy(
                        query = it.query,
                        editor = it.editor,
                        confirmDelete = it.confirmDelete,
                        mutationInFlight = it.mutationInFlight,
                        mutationFailure = it.mutationFailure,
                    ),
                    intent,
                )
                it.copy(
                    query = next.query,
                    editor = next.editor,
                    confirmDelete = next.confirmDelete,
                    mutationFailure = next.mutationFailure,
                )
            }
        }
    }

    /** Record that this screen was shown. */
    fun onShown() {
        analytics.record(AnalyticsEvent.ScreenShown(TaffyDestination.Memory.screenId))
    }

    private fun requestSearch(value: String) {
        searchJob?.cancel()
        if (value.trim().isEmpty()) return
        searchJob = viewModelScope.launch {
            delay(SEARCH_DEBOUNCE_MILLIS)
            repository.search(value)
        }
    }

    private fun settleMutation(
        submitted: MemoryUiState.Editor,
        operation: MemoryUiState.MutationOperation,
        result: TaffyResult<Unit>,
    ) {
        local.update { current ->
            val editor = current.editor
            if (editor == null || !editor.sameTargetAs(submitted)) {
                return@update current.copy(mutationInFlight = false)
            }
            when (result) {
                is TaffyResult.Success -> current.copy(
                    editor = null,
                    confirmDelete = false,
                    mutationInFlight = false,
                    mutationFailure = null,
                )
                is TaffyResult.Failure -> current.copy(
                    mutationInFlight = false,
                    mutationFailure = MemoryUiState.MutationFailure(operation, result.reason),
                )
            }
        }
    }

    private data class Local(
        val query: String = "",
        val editor: MemoryUiState.Editor? = null,
        val confirmDelete: Boolean = false,
        val mutationInFlight: Boolean = false,
        val mutationFailure: MemoryUiState.MutationFailure? = null,
    )

    private companion object {
        const val QUERY_KEY = "memory_query"
        const val SUBSCRIPTION_TIMEOUT_MILLIS = 5_000L
        const val SEARCH_DEBOUNCE_MILLIS = 250L
    }
}

private fun MemoryUiState.Editor.sameTargetAs(other: MemoryUiState.Editor): Boolean =
    id == other.id &&
        expectedMemoryRevision == other.expectedMemoryRevision &&
        expectedRecordRevision == other.expectedRecordRevision

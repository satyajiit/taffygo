// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.assistant

import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.taffygo.browser.ui.core.common.TaffyResult
import com.taffygo.browser.ui.core.task.TaskInputRepository
import kotlinx.coroutines.Job
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.collectLatest
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch

/**
 * The form sheet's one source of truth, and the only place a typed value lives.
 *
 * Three things this class deliberately does not have, each of which would put a
 * card number or a one-time code somewhere it outlives the sheet:
 *
 *  - **no `SavedStateHandle`.** Every other view model in this feature takes
 *    one, because restoring a goal or a search after process death is a
 *    kindness. Restoring a one-time code is a value written to disk by the
 *    platform, so this screen forgets instead, and a person retypes.
 *  - **no analytics.** Not even a screen-shown event: this surface appears
 *    exactly when a task is waiting on a value, so recording that it appeared
 *    records what kind of thing the person was asked for and when.
 *  - **no logging of its state.** `TaskInputUiState.toString` is overridden for
 *    the same reason, so even a debugger's dump of the flow carries none.
 *
 * What is typed goes one place: [TaskInputRepository.submit], which hands it to
 * the browser's vault, and is then dropped by clearing the rows.
 */
class TaskInputViewModel(
    private val forms: TaskInputRepository,
) : ViewModel() {

    private val internalState = MutableStateFlow(
        projectTaskInput(forms.request.value, forms.isAvailable),
    )

    /** What the sheet renders. */
    val state: StateFlow<TaskInputUiState> = internalState.asStateFlow()

    private var submission: Job? = null

    init {
        viewModelScope.launch {
            forms.request.collectLatest { request ->
                internalState.update { previous ->
                    projectTaskInput(request, forms.isAvailable, previous)
                }
            }
        }
    }

    /** Act on something the person did. */
    fun onIntent(intent: TaskInputIntent) {
        internalState.update { reduceTaskInput(it, intent) }
        when (intent) {
            TaskInputIntent.Confirm -> submit()
            TaskInputIntent.CompleteInteractive -> completeInteractive()
            else -> Unit
        }
    }

    private fun submit() {
        val current = internalState.value
        if (!current.canConfirm || submission != null) return
        // Built here and handed straight on: the map is a local, it is not put
        // back into the state, and nothing keeps a reference to it afterwards.
        val values = current.rows.associate { it.id to it.value }
        internalState.update { it.copy(submitting = true, failure = null) }
        submission = viewModelScope.launch {
            finish(forms.submit(current.requestId, values))
        }
    }

    private fun completeInteractive() {
        val current = internalState.value
        if (!current.canCompleteInteractive || submission != null) return
        internalState.update { it.copy(submitting = true, failure = null) }
        submission = viewModelScope.launch {
            finish(forms.completeInteractive(current.requestId))
        }
    }

    /**
     * What an answered request leaves behind, which on success is nothing.
     *
     * The rows are emptied rather than left for the browser's next description
     * to replace, because the two are a moment apart and in that moment the
     * sheet is still holding what was typed with the request already spent.
     */
    private fun finish(result: TaffyResult<Unit>) {
        submission = null
        internalState.update { current ->
            when (result) {
                is TaffyResult.Success -> TaskInputUiState()
                is TaffyResult.Failure -> current.copy(
                    submitting = false,
                    failure = result.reason,
                )
            }
        }
    }
}

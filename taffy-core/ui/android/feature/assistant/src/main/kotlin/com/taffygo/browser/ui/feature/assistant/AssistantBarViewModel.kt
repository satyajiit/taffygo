// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.assistant

import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.taffygo.browser.ui.core.model.TaskControl
import com.taffygo.browser.ui.core.task.TaffyReadinessRepository
import com.taffygo.browser.ui.core.task.TaskControlRefusal
import com.taffygo.browser.ui.core.task.TaskRepository
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyNavigator
import java.util.UUID
import javax.inject.Inject
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.flow.stateIn
import kotlinx.coroutines.launch

/**
 * Screen SCR-301's one source of truth.
 *
 * State and commands both cross the generated Core API. Compose never runs or
 * imitates a task transition.
 */
class AssistantBarViewModel @Inject constructor(
    private val tasks: TaskRepository,
    readiness: TaffyReadinessRepository,
) : ViewModel() {

    /** What screen SCR-301 renders. */
    val state: StateFlow<AssistantBarUiState> =
        combine(tasks.state, readiness.readiness) { repository, verdict ->
            withdrawRefusedControl(
                projectAssistantBar(repository, readiness = verdict),
                repository.controlRefusal,
            )
        }.stateIn(
            scope = viewModelScope,
            started = SharingStarted.WhileSubscribed(SUBSCRIPTION_TIMEOUT_MILLIS),
            initialValue = withdrawRefusedControl(
                projectAssistantBar(
                    tasks.state.value,
                    readiness = readiness.readiness.value,
                ),
                tasks.state.value.controlRefusal,
            ),
        )

    /** Act on something the user did. */
    fun onIntent(intent: AssistantBarIntent, navigator: TaffyNavigator) {
        when (intent) {
            is AssistantBarIntent.Control -> useControl(intent.control)
            AssistantBarIntent.OpenAssistant -> navigator.goTo(assistantDestination(state.value))
            AssistantBarIntent.ReviewApproval -> navigator.goTo(TaffyDestination.TaskView)
            // The results of *this* task, which is the task view (UX spec
            // section 6): its answer, its sources, its timeline, and the
            // footer holding Save, Discard and Put this away. It used to be
            // the workspace list, which is the library of research a person
            // has kept — a different surface, reached from the dock's own
            // Workspaces door. For an errand that keeps nothing the library
            // is empty, so the one control an ended task's pill offered led
            // to "No workspaces yet" and the task's own surface, with the
            // only way to be done with it, could not be reached at all
            // (decision 0180).
            AssistantBarIntent.OpenResults -> navigator.goTo(TaffyDestination.TaskView)
            AssistantBarIntent.CompleteHandover -> handBack()
            is AssistantBarIntent.Answer -> answer(intent.answer)
        }
    }

    /**
     * Give the page back to Taffy: the one door a hand-over closes through.
     * Read off the repository's task rather than the rendered state, so a
     * tap that lands after the wait has moved on completes nothing.
     */
    private fun handBack() {
        val task = tasks.state.value.task ?: return
        if (!task.hasHandover) return
        viewModelScope.launch { tasks.completeHandover(task.id) }
    }

    private fun answer(answer: String) {
        val task = tasks.state.value.task ?: return
        if (!task.hasAsk) return
        viewModelScope.launch { tasks.supplyUserInput(task.id, answer) }
    }

    private fun useControl(control: TaskControl) {
        val rendered = state.value
        val taskId = rendered.taskId ?: return
        val taskRevision = rendered.taskRevision ?: return
        if (control !in rendered.controls) return
        viewModelScope.launch {
            tasks.useControl(taskId, taskRevision, control)
        }
    }

    private companion object {
        const val SUBSCRIPTION_TIMEOUT_MILLIS = 5_000L
    }
}

/**
 * Takes a control off the pill once the browser has refused it.
 *
 * The pill is one line and decision 0141 keeps it that way, so it has nowhere
 * to put the sentence the task view now draws. What it can do is stop offering
 * a button that has already been answered no — the person is left with the
 * controls that work, and the explanation waits on the screen the pill opens.
 * Scoped to the exact task revision by the repository, so a control refused at
 * one revision is offered again at the next (decision 0221).
 */
internal fun withdrawRefusedControl(
    state: AssistantBarUiState,
    refusal: TaskControlRefusal?,
): AssistantBarUiState =
    if (refusal == null ||
        refusal.taskId != state.taskId ||
        refusal.taskRevision != state.taskRevision ||
        refusal.control !in state.controls
    ) {
        state
    } else {
        state.copy(controls = state.controls.filterNot { it == refusal.control })
    }

/** An idle invitation opens Ask; an existing task opens its detail. */
internal fun assistantDestination(state: AssistantBarUiState): TaffyDestination =
    if (state.state == null) {
        TaffyDestination.AssistantBar(
            question = "",
            askId = UUID.randomUUID().toString(),
        )
    } else {
        TaffyDestination.TaskView
    }

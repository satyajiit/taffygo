// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.task

import com.taffygo.browser.ui.core.model.AssistantMode
import com.taffygo.browser.ui.core.model.Fact
import com.taffygo.browser.ui.core.model.SourceRecord
import com.taffygo.browser.ui.core.model.TaskControl
import com.taffygo.browser.ui.core.model.TaskDisplayState
import com.taffygo.browser.ui.core.model.TaskTemplate
import com.taffygo.browser.ui.core.model.TaskTimelineEntry
import taffy.core_api.CoreFailureCode
import taffy.core_api.TaskPhase

/** Android-facing projection of generated task state; it contains no task reducer. */
data class TaskProjection(
    val id: String,
    val revision: ULong,
    val phase: TaskPhase,
    val goal: String,
    val template: TaskTemplate,
    val progressBasisPoints: UInt,
    val statusMessageKey: String?,
    val failure: CoreFailureCode?,
    val pendingAction: PendingActionProjection?,
    val askPrompt: String? = null,
    /**
     * The opaque identity of the form Taffy is holding open, or null.
     *
     * The contract's own field, carried through unread: it is a name the
     * browser minted for a request, and this layer's only business with it is
     * to hand it back when the person has filled the form in.
     */
    val inputRequest: String? = null,
    /** Exact controls admitted by the core for this task revision. */
    val allowedControls: List<TaskControl> = emptyList(),
    /** Content-free history derived from the task's committed workspace. */
    val timeline: List<TaskTimelineEntry> = emptyList(),
    /** Every committed source belonging to this task's workspace. */
    val sources: List<SourceRecord> = emptyList(),
    /** Every committed, source-labelled output fact. */
    val facts: List<Fact> = emptyList(),
    /** The task's temporary-or-saved workspace identity, when output exists. */
    val workspaceId: String? = null,
    /** Exact workspace revision backing this projection. */
    val workspaceRevision: ULong? = null,
    /** Whether the person has already explicitly kept this result. */
    val workspaceSaved: Boolean = false,
    /** Whether this exact terminal result may be saved now. */
    val canSaveWorkspace: Boolean = false,
    /** Whether this exact terminal unsaved result may be explicitly discarded now. */
    val canDiscardWorkspace: Boolean = false,
    /**
     * The newest answer in [conversation], for a surface that shows one; never
     * durable task state.
     */
    val liveAnswer: TaskAnswerProjection? = null,
    /**
     * What was asked of this task from this process and what came back, or
     * null while nothing has. Visible answer residency, never durable.
     */
    val conversation: TaskConversationProjection? = null,
    /** Reproducible file metadata; file bytes never enter this snapshot. */
    val artifacts: List<TaskArtifactProjection> = emptyList(),
) {
    /**
     * The one thing this task is waiting on a person for, or null.
     *
     * The three waits are one closed answer rather than three independent
     * questions, because they are three doors and only one of them is open. A
     * surface that asked each in turn could draw two at once, which is a screen
     * telling a person to do two different things about the same task.
     *
     * **A field-value wait is read off [inputRequest] and not off
     * [statusMessageKey].** The key is a *message*: what the browser would say
     * if it had to say something, chosen by the core's own precedence among the
     * waits it is holding. Comparing it to a literal made this layer depend on
     * that precedence and on the spelling of a string in another language's
     * source — so a wait that was genuinely open was invisible here whenever
     * the core had a better sentence to print. The contract now carries the
     * fact itself, and a fact beats a sentence about it.
     *
     * The other two are still read from the key, because the contract carries
     * nothing else for them yet. The order below is the core's own, and
     * deliberately so (see `project_status_message` in the core runtime): a
     * handover is a wait a person cannot answer from a sheet, so where one is
     * open beside a form, saying "fill this in" would point at the wrong door.
     */
    val wait: Wait? = when {
        statusMessageKey == HANDOVER_MESSAGE_KEY -> Wait.HANDOVER
        inputRequest != null -> Wait.FIELD_VALUES
        statusMessageKey == ANSWER_MESSAGE_KEY -> Wait.ANSWER
        else -> null
    }

    /** Whether Taffy has handed the page back and is waiting on the person. */
    val hasHandover: Boolean
        get() = wait == Wait.HANDOVER

    /** Whether Taffy asked a question and is waiting on the answer. */
    val hasAsk: Boolean
        get() = wait == Wait.ANSWER

    /** Whether Taffy is holding a form open for the person to fill in. */
    val hasInputRequest: Boolean
        get() = wait == Wait.FIELD_VALUES

    val displayState: TaskDisplayState? = when (phase) {
        TaskPhase.IDLE -> null
        TaskPhase.PLANNING,
        TaskPhase.RUNNING,
        -> TaskDisplayState.RUNNING
        TaskPhase.WAITING_FOR_USER -> TaskDisplayState.WAITING_FOR_YOU
        TaskPhase.PAUSED -> TaskDisplayState.PAUSED
        TaskPhase.COMPLETED -> TaskDisplayState.DONE
        TaskPhase.PARTIAL -> TaskDisplayState.PARTLY_DONE
        TaskPhase.CANCELLED -> TaskDisplayState.STOPPED
        TaskPhase.FAILED,
        TaskPhase.OUTCOME_UNKNOWN,
        -> TaskDisplayState.FAILED
    }

    val isUnderWay: Boolean = when (phase) {
        TaskPhase.PLANNING,
        TaskPhase.WAITING_FOR_USER,
        TaskPhase.RUNNING,
        -> true
        TaskPhase.IDLE,
        TaskPhase.COMPLETED,
        TaskPhase.PARTIAL,
        TaskPhase.FAILED,
        TaskPhase.CANCELLED,
        TaskPhase.OUTCOME_UNKNOWN,
        TaskPhase.PAUSED,
        -> false
    }

    val mode: AssistantMode =
        if (isUnderWay) AssistantMode.TAFFY_BROWSES else AssistantMode.YOU_BROWSE

    /**
     * Whether the person can ask this task something more.
     *
     * A finished task keeps its conversation for the session (decision 0137):
     * done and partly done are the two states the core admits a follow-up
     * from, and a task that failed, was stopped, or is still working is not
     * one to ask again — the first two are started afresh, the last waits.
     */
    val acceptsFollowUp: Boolean = when (phase) {
        TaskPhase.COMPLETED,
        TaskPhase.PARTIAL,
        -> true
        TaskPhase.IDLE,
        TaskPhase.PLANNING,
        TaskPhase.RUNNING,
        TaskPhase.WAITING_FOR_USER,
        TaskPhase.PAUSED,
        TaskPhase.FAILED,
        TaskPhase.CANCELLED,
        TaskPhase.OUTCOME_UNKNOWN,
        -> false
    }

    /** The three doors a task can be held at, of which at most one is open. */
    enum class Wait {
        /** The page is the person's until they say they are finished with it. */
        HANDOVER,

        /** A form is open and Taffy is waiting for what goes in it. */
        FIELD_VALUES,

        /** Taffy asked something and is waiting to be told. */
        ANSWER,
    }

    private companion object {
        const val HANDOVER_MESSAGE_KEY = "task.waiting_for_handover"
        const val ANSWER_MESSAGE_KEY = "task.waiting_for_input"
    }
}

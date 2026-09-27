// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.assistant

import com.taffygo.browser.ui.core.model.AssistantMode
import com.taffygo.browser.ui.core.model.TaskControl
import com.taffygo.browser.ui.core.model.TaskDisplayState
import com.taffygo.browser.ui.core.model.TaskTimelineEntry
import com.taffygo.browser.ui.core.task.TaffyReadiness

/**
 * Screen SCR-301 — the Assistant bar, one line of truth about one assistant.
 *
 * The eight bar states of UX spec section 4 are represented exactly: [state] is
 * null while the bar is idle or listening, and [listening] separates those two.
 * There is no ninth, and nothing here can hold a state the task machine cannot
 * produce.
 */
data class AssistantBarUiState(
    /** Opaque task identity whose controls were projected. */
    val taskId: String? = null,
    /** Exact task revision whose controls were projected. */
    val taskRevision: ULong? = null,
    /** What a person sees, or null while there is no task under way. */
    val state: TaskDisplayState? = null,
    /** Whether the user is actively typing a request. Never a background state. */
    val listening: Boolean = false,
    /** The mode chip, which is absent in the default mode. */
    val mode: AssistantMode = AssistantMode.YOU_BROWSE,
    /** How many sources the task has read. */
    val sourcesRead: Int = 0,
    /**
     * How many sources the task means to read, or nought when it means to read
     * no fixed number — an errand, which finds its own.
     */
    val sourcesPlanned: Int = 0,
    /**
     * The newest step of the task's timeline, when it has one.
     *
     * A running task's line follows its steps as they land — read this page,
     * built the output — so the bar moves while Taffy works. Each step is a
     * local template with a host and a count in it, never page or model text,
     * exactly as the task view draws them.
     */
    val latestStep: TaskTimelineEntry? = null,
    /** How many facts two sources disagree about. */
    val conflicts: Int = 0,
    /** The origin a pending approval touches, when one is pending. */
    val approvalHost: String? = null,
    /** How many things a pending approval covers. */
    val approvalCount: Int = 0,
    /** Whether Taffy has handed the page back and is waiting on the person. */
    val hasHandover: Boolean = false,
    /** Whether Taffy asked the person for a value and is waiting on the answer. */
    val hasAsk: Boolean = false,
    /** Whether Taffy is waiting on field values, which the field-value sheet collects. */
    val hasInputRequest: Boolean = false,
    /** Taffy's question, in the words the core gave it, when [hasAsk]. */
    val askPrompt: String? = null,
    /** The controls shown, in the order every surface shows them. */
    val controls: List<TaskControl> = emptyList(),
    /**
     * What the bar has to say that the task cannot say for itself.
     *
     * Null is the honest default for a state built by hand; the projection is
     * the only thing that produces one, from the seam's own answer.
     */
    val notice: TaskNotice? = null,
    /** Why the task failed, when it did; the failed line reads it. */
    val failure: TaskFailureReason? = null,
    /** Whether Taffy can reach a provider; the idle line reads it (ux-spec section 4, Not set up). */
    val readiness: TaffyReadiness = TaffyReadiness.Unknown,
    /**
     * The core's own word for what the task is doing right now — queued,
     * planning, reading a page, thinking, acting — as the closed key it
     * publishes, or null. The running line reads it before the timeline,
     * because the newest step says what Taffy has done and this says what it
     * is doing.
     */
    val statusMessageKey: String? = null,
    /** How far along the core says the task is, in basis points; nought until it says. */
    val progressBasisPoints: Int = 0,
) {
    /** Whether the bar is collapsed to its slim affordance. */
    val isIdle: Boolean
        get() = state == null && !listening

    /** An idle pill on a phone where nothing is set up says so, and its tap still opens the sheet. */
    val invitesSetup: Boolean
        get() = isIdle && readiness.needsSetup

    /**
     * Whether what was read may be counted as "N of M".
     *
     * Only against a plan, and only while the count fits inside it. A task
     * with no plan has no denominator, and a count that has outgrown its plan
     * has shown the plan was not what the task read from — either way the line
     * names what was read and nothing else, because "3 of 1" is a number the
     * product does not hold.
     */
    val countsReadAgainstPlan: Boolean
        get() = sourcesPlanned > 0 && sourcesRead <= sourcesPlanned

    /**
     * Whether the bar's one line has to be the notice rather than the state's
     * own words.
     *
     * Six of the eight lines of UX spec section 4 describe a task that is under
     * way — "Taffy is comparing 3 pages…" most of all — and where nothing is
     * driving the task not one of them is true. A finished task keeps its own
     * line, because "You stopped Taffy" is exactly what happened and the whole
     * account of why nothing was read is one tap away on SCR-303.
     */
    val speaksNotice: Boolean
        get() = notice != null && state != null && !state.isFinal
}

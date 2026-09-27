// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.assistant

import com.taffygo.browser.ui.core.common.FailureReason
import com.taffygo.browser.ui.core.model.AssistantMode
import com.taffygo.browser.ui.core.model.Fact
import com.taffygo.browser.ui.core.model.SourceRecord
import com.taffygo.browser.ui.core.model.TaskControl
import com.taffygo.browser.ui.core.task.TaskControlRefusal
import com.taffygo.browser.ui.core.model.TaskDisplayState
import com.taffygo.browser.ui.core.model.TaskTimelineEntry
import com.taffygo.browser.ui.core.task.TaskAnswerProjection
import com.taffygo.browser.ui.core.task.TaskArtifactProjection

/**
 * Screen SCR-303 — the full-screen truth about one task.
 *
 * The header, timeline, sources, output, and controls of UX spec section 6, in
 * that order. Every cause is a typed value rather than a message, so the screen
 * composes every status sentence from a local template. The one content
 * exception is [liveAnswer]: visible model text already decoded and sanitized
 * by the isolated core, carried in a panel that cannot be mistaken for a
 * source-labelled workspace fact.
 */
data class TaskViewUiState(
    /** Opaque task identity whose controls were projected. */
    val taskId: String? = null,
    /** Exact task revision whose controls were projected. */
    val taskRevision: ULong? = null,
    /** The goal in the user's own words. */
    val goal: String = "",
    /** What a person sees, or null while the task is in setup and consent. */
    val state: TaskDisplayState? = null,
    /** The mode chip. */
    val mode: AssistantMode = AssistantMode.YOU_BROWSE,
    /** Every step, newest first, as UX spec section 6 shows them. */
    val timeline: List<TaskTimelineEntry> = emptyList(),
    /** Every page the task read. */
    val sources: List<SourceRecord> = emptyList(),
    /** The output as it builds. */
    val facts: List<Fact> = emptyList(),
    /** Process-resident answer text, separate from durable sourced output. */
    val liveAnswer: TaskAnswerProjection? = null,
    /** Person-controlled playback of that final visible answer. Never durable. */
    val readAloud: ReadAloudUiState = ReadAloudUiState.Idle,
    /** Reproducible file metadata; bytes never enter this snapshot. */
    val artifacts: List<TaskArtifactProjection> = emptyList(),
    /** Artifact currently waiting on the browser or an Android destination. */
    val artifactExportingId: String? = null,
    /** One exact transient file ready for the trusted platform handoff. */
    internal val pendingArtifactExport: PendingTaskArtifactExport? = null,
    /** Whether the most recent file handoff failed without changing task state. */
    val artifactExportFailed: Boolean = false,
    /** Whether this exact terminal result can be explicitly kept now. */
    val canSaveWorkspace: Boolean = false,
    /** Whether the exact terminal temporary result can be explicitly discarded. */
    val canDiscardWorkspace: Boolean = false,
    /** Whether the destructive cleanup confirmation is visible. */
    val showDiscardConfirmation: Boolean = false,
    /**
     * Whether a save or discard has been admitted and no state has landed yet.
     *
     * The core answers a workspace command on admission, not on completion, so
     * the durable delete arrives on a later publication. Without this the
     * confirm tap produced nothing a person could see, which is
     * indistinguishable from a refusal.
     */
    val workspaceChangeInFlight: Boolean = false,
    /**
     * Why the last save or discard was refused, or null. DUPLICATE means the
     * same change is still running and is worth its own sentence; every other
     * reason reads as one refusal, because a person cannot act differently on
     * the difference between them.
     */
    val workspaceChangeRefusal: FailureReason? = null,
    /**
     * Which control the person pressed that the browser would not take, or
     * null.
     *
     * The control list comes from the core, which knows the task's state and
     * not the browser's authority over it. After a browser restart the core
     * restores a paused task and still offers Resume — its own rehydration
     * comment calls that control "the inert state marker" — while the consent
     * that would honour it lived only in the browser session that is gone. The
     * submission is refused at `authority/resume`, and this screen used to
     * drop the answer, so Resume looked pressed and nothing moved (decision
     * 0221).
     */
    val controlRefusal: TaskControlRefusal? = null,
    /** Whether the person already chose to keep this task result. */
    val workspaceSaved: Boolean = false,
    /** The controls shown, in the order every surface shows them. */
    val controls: List<TaskControl> = emptyList(),
    /** Opaque action identity returned only as an approval intent. */
    val approvalActionId: String? = null,
    /** The origin a pending approval touches. */
    val approvalHost: String? = null,
    /** How many things a pending approval covers. */
    val approvalCount: Int = 0,
    /** Whether Taffy has handed the page back and is waiting on the person. */
    val hasHandover: Boolean = false,
    /** Whether Taffy asked the person for a value and is waiting on the answer. */
    val hasAsk: Boolean = false,
    /** The classified question, when this process still holds it. */
    val askPrompt: String? = null,
    /**
     * What the screen has to explain that the task cannot explain itself.
     *
     * Null is "there is nothing to add", which is the honest default for a
     * state built by hand — a preview or a test — because a notice invented
     * here would be a sentence about a build nobody asked about. The projection
     * is the only thing that produces one, from the seam's own answer.
     */
    val notice: TaskNotice? = null,
    /** Whether the circuit is open and an explicit retry is available. */
    val retryAvailable: Boolean = false,
    /** Why the task failed, when it did and the core said. */
    val failure: TaskFailureReason? = null,
    /** Whether Not now took the set-up offer away for this task. */
    val setupDismissed: Boolean = false,
    /**
     * One Memory suggestion after a finished task, or null. Never more than
     * one, and never while the task is still under way.
     */
    val rememberThis: RememberThisSuggestion? = null,
) {
    /** Whether there is an approval on the table. */
    val hasApproval: Boolean
        get() = approvalActionId != null

    /** A task that failed on its provider — unreachable, or a refused key — offers set-up, until Not now. */
    val offersSetup: Boolean
        get() = state == TaskDisplayState.FAILED &&
            failure?.offersSetup == true &&
            !setupDismissed

    /** Whether the output panel has anything in it yet. */
    val hasOutput: Boolean
        get() = facts.isNotEmpty()

    /**
     * How many pages the task read, counted the way the bar counts them.
     *
     * Not the size of [sources]. That is the workspace's list — every page
     * consented to at the start, read or not, plus those whose reads it kept —
     * which is a different record from what the task did, so a failed errand
     * that read three sites could show 1 here beside a bar that counts 3.
     */
    val pagesRead: Int
        get() = pagesReadIn(timeline)

    /** Whether a person can start playback of the answer currently on screen. */
    val canReadAnswerAloud: Boolean
        get() = canReadTaskAnswer(this)

    /**
     * Whether the timeline would draw a person nothing at all.
     *
     * This is the defect, stated as a property so a test can forbid it: a task
     * that had been started sat under "Running" with a timeline reading
     * "Nothing has happened yet", and nothing on the screen ever said why or
     * ever would. A started task is allowed an empty timeline — a first page
     * takes a moment — but only while something is on its way. Where nothing
     * is, [notice] carries the sentence that says so, and this answers `false`
     * because the timeline now has a line in it.
     */
    val timelineSaysNothing: Boolean
        get() = state != null && timeline.isEmpty() && notice == null
}

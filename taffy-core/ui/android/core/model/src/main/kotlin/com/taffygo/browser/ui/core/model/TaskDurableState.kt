// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.model

/**
 * The durable task state projected from `task-engine`'s task state (domain model
 * section 9.2). Thirteen states, in the order the specification's diagram
 * introduces them.
 *
 * The internal name is never shown to a person. [display] is the whole of the
 * section 9.2 display table as one pure function, and it answers null for a
 * task still in setup and consent, because those are surfaces rather than task
 * states.
 */
enum class TaskDurableState(
    /** A short, compiled-in name, safe to record in an audit event. */
    val label: String,
) {
    /** Being composed. Nothing has been consented to. */
    DRAFT("DRAFT"),

    /** Waiting on the user: the initial consent, or an in-task approval. */
    AWAITING_CONSENT("AWAITING_CONSENT"),

    /** Consented and waiting for an executor. */
    QUEUED("QUEUED"),

    /** Executing. */
    RUNNING("RUNNING"),

    /** Stopped on a missing source or a missing input. */
    WAITING_USER("WAITING_USER"),

    /** Revoking authority and settling in-flight work, on the way to held. */
    PAUSING("PAUSING"),

    /** Held. No authority stands. */
    PAUSED("PAUSED"),

    /** Revoking authority and settling the journal, on the way to stopped. */
    CANCELLING("CANCELLING"),

    /** A result candidate is being validated and persisted. */
    COMPLETING("COMPLETING"),

    /** Terminal: stopped by the user. */
    CANCELLED("CANCELLED"),

    /** Terminal: a validated complete result. */
    COMPLETED("COMPLETED"),

    /** Terminal: a useful result with labelled gaps. */
    PARTIAL("PARTIAL"),

    /** Terminal: it could not be finished. */
    FAILED("FAILED"),
    ;

    /** Whether the task has ended. A rerun is a new task, never this one. */
    val isTerminal: Boolean
        get() = this in TERMINAL

    /** Whether the task is revoking authority and settling in-flight work. */
    val isSettling: Boolean
        get() = this == PAUSING || this == CANCELLING

    /** What a person sees, or null while the task is still in setup and consent. */
    fun display(): TaskDisplayState? = when (this) {
        DRAFT, AWAITING_CONSENT -> null
        QUEUED, RUNNING, COMPLETING -> TaskDisplayState.RUNNING
        WAITING_USER -> TaskDisplayState.WAITING_FOR_YOU
        PAUSING, PAUSED -> TaskDisplayState.PAUSED
        COMPLETED -> TaskDisplayState.DONE
        PARTIAL -> TaskDisplayState.PARTLY_DONE
        CANCELLING, CANCELLED -> TaskDisplayState.STOPPED
        FAILED -> TaskDisplayState.FAILED
    }

    companion object {
        /** The four terminal states. */
        val TERMINAL: Set<TaskDurableState> = setOf(CANCELLED, COMPLETED, PARTIAL, FAILED)
    }
}

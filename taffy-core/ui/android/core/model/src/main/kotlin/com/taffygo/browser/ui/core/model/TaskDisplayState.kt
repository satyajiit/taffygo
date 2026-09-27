// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.model

/**
 * The seven task states a person sees (UX spec section 6, domain model section
 * 9.2, decision 0010). There is no eighth: setup and consent are surfaces, not
 * task states, so [TaskDurableState.display] answers null for them rather than
 * inventing a member here.
 *
 * The words themselves are string resources, because every string is
 * externalized (parity row PAR-L10N-001). This type carries the identity of a
 * state and its audit label; the presentation layer carries its wording, its
 * icon, and its colour — and it carries all three, because status is never
 * conveyed by colour alone (parity row PAR-A11Y-004).
 */
enum class TaskDisplayState(
    /** A short, compiled-in name, safe to record in an audit event. */
    val label: String,
) {
    /** Taffy is working. */
    RUNNING("running"),

    /** Taffy needs something only the user can give it. */
    WAITING_FOR_YOU("waiting_for_you"),

    /** Held, indefinitely, until the user resumes it. */
    PAUSED("paused"),

    /** A validated complete result. */
    DONE("done"),

    /** A useful result with labelled gaps. Never rounded up to done. */
    PARTLY_DONE("partly_done"),

    /** What the user did. */
    STOPPED("stopped"),

    /** What happened to Taffy. Never conflated with stopped. */
    FAILED("failed"),
    ;

    /** Whether the task has ended in this state. */
    val isFinal: Boolean
        get() = this == DONE || this == PARTLY_DONE || this == STOPPED || this == FAILED
}

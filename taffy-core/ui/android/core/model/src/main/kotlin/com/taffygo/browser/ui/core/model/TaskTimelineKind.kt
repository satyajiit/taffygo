// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.model

/**
 * The closed set of timeline steps. A closed set is what keeps the timeline in
 * plain language: each member has one string resource behind it, so no step can
 * ever render jargon, a tool name, or model talk (UX spec section 6).
 *
 * Nine, and they are the core's own nine (decision 0148). This used to hold two
 * — the only two the wire carried, because the timeline was derived on this
 * side from the workspace's sources and facts. That derivation is why an
 * errand's timeline was empty, why a failed task's timeline was empty in the
 * one case it matters most, and why a page read three times appeared once in
 * the order facts landed rather than the order Taffy acted. The task now
 * carries its own bounded record of what it did, appended where the reducer
 * commits its transitions, and this enumeration is that record's kinds.
 *
 * `sign_in_needed` and `authority_revoked`, which stood in an older set of
 * five, do not return: a sign-in is a hand-back and reads as one, and revoked
 * authority is a task state the header already says.
 */
enum class TaskTimelineKind(
    /** A short, compiled-in name, safe to record in an audit event. */
    val label: String,
) {
    /** A page was opened. */
    OPENED_PAGE("opened_page"),

    /** A page was read. */
    READ_PAGE("read_page"),

    /** A page would not load, or could not be read. */
    PAGE_UNAVAILABLE("page_unavailable"),

    /** A move was refused, by policy or by the browser. */
    MOVE_REFUSED("move_refused"),

    /** The person was asked for a decision, a value or a permission. */
    ASKED_YOU("asked_you"),

    /** The person answered. */
    YOU_ANSWERED("you_answered"),

    /** Taffy handed the page over for the person to work. */
    HANDED_BACK("handed_back"),

    /** The person took the page over. */
    YOU_TOOK_OVER("you_took_over"),

    /** The output was assembled from the accepted facts. */
    BUILT_OUTPUT("built_output"),
}

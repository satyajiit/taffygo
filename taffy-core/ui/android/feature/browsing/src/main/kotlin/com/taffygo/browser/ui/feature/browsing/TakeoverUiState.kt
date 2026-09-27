// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import com.taffygo.browser.ui.core.model.TaskInputField

/**
 * What the browsing surface says while Taffy is the one driving it.
 *
 * Separate claims rather than one flag, because they become true and stop being
 * true at different moments: the page is Taffy's, the task is held waiting on
 * the person, there is a particular place on the page they have to touch, and
 * the bottom row has a task to speak for whether or not it has ended. A surface
 * with one flag would have had to guess at the rest.
 */
data class TakeoverUiState(
    /** Opaque task identity whose controls were projected. */
    val taskId: String? = null,
    /** Exact task revision whose controls were projected. */
    val taskRevision: ULong? = null,
    /**
     * Whether Taffy is working in this window.
     *
     * The frame and the band are drawn from this alone. It is the task's own
     * `isUnderWay`, so a task that has finished, failed or been stopped takes
     * the frame off the page in the same breath as it stops working — there is
     * no separate "and now put it away" for anything to miss.
     */
    val active: Boolean = false,
    /**
     * Whether there is a task at all, in any of its seven states.
     *
     * A fourth claim rather than a widening of [active], for the reason the
     * three above are separate: [active] is the task's own `isUnderWay` and
     * goes false the moment work stops, which is exactly right for the frame
     * and the band and exactly wrong for the chrome row — a task that has
     * finished still has one thing left to say, and the assistant pill is where
     * it says it. The start page's dock gives way to that pill for as long as
     * this is true (decision 0140).
     */
    val hasTask: Boolean = false,
    /**
     * Whether that task has finished, in any of the four ways it can.
     *
     * The pair with [hasTask] is what the chrome row is drawn from, and it
     * needs both because the row has three shapes rather than two: no task at
     * all is the four-slot dock or the page's own back, forward and tabs; a
     * task still going is the assistant pill alone, because none of those
     * controls is allowed while Taffy is driving and a disabled control in the
     * position nearest the thumb is worse than an absent one; and a task that
     * has ended is the pill with the controls back around it, because that is
     * the moment the page is the person's again (decision 0141).
     */
    val taskEnded: Boolean = false,
    /** Whether the task is held waiting on the person right now. */
    val waitingForYou: Boolean = false,
    /** Whether this exact task revision admits giving the page back. */
    val canTakeOver: Boolean = false,
    /**
     * Where on the page a widget only the person can work is, or null.
     *
     * Present only when the page in front of them is the page the widget is on
     * — see the projection. A cut-out drawn over a different page points at
     * nothing and hides part of what is there.
     */
    val highlight: TaskInputField.Highlight? = null,
) {
    /** Whether the page area draws the accent frame. */
    val showsFrame: Boolean
        get() = active

    /** Whether the page area draws the cut-out. */
    val showsHighlight: Boolean
        get() = active && highlight != null

    /**
     * Whether the page area holds the page against the person's touch.
     *
     * True exactly while Taffy is the one working it and has not asked for
     * them: two drivers on one page is how a read and the action it authorized
     * come to disagree. A task that is waiting for the person is not working
     * the page, so the hold ends the moment Taffy hands it back, and a marked
     * widget is that hand-back made visible — which is why a highlight lifts
     * the hold rather than sitting under it.
     */
    val showsLock: Boolean
        get() = active && !waitingForYou && highlight == null
}

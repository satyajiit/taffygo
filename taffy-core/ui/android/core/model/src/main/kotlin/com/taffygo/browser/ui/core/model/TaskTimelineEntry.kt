// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.model

/**
 * One step of the task timeline (UX spec section 6), newest first in the view.
 *
 * The wording is composed from a trusted local template with the host and the
 * counts filled in — never from page text and never from model text.
 */
data class TaskTimelineEntry(
    /**
     * Which step this is over the task's whole life, counted from one.
     *
     * It does not restart when the core's bounded record drops its oldest step,
     * so a surface can tell the first thing that happened from the oldest thing
     * still kept. A `Long` because the core counts in one: narrowing it would
     * be a silent reordering of the one field the order is read from.
     */
    val sequence: Long,
    /** What happened. */
    val kind: TaskTimelineKind,
    /** The host the step touched, when it touched one. */
    val host: String? = null,
    /** How many facts, pages, or fields the step accounted for. */
    val count: Int = 0,
    /** When it happened, in epoch milliseconds. */
    val atEpochMillis: Long = 0,
)

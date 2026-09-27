// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.task

/**
 * Why a composer cannot start the task it describes, in a closed vocabulary a
 * surface turns into one sentence under its own box.
 */
enum class TaskStartRefusal {
    /** Nothing in the person's own words to start from. */
    NO_GOAL,
    /** The shape needs a model and no provider can answer for one. */
    SETUP_NEEDED,
    /** The core has not said whether a provider can answer yet. */
    NOT_READY_YET,
    /** The browser is not answering, or is asking for a retry. */
    CORE_NOT_READY,
    /** One task at a time; this composer waits for the one under way. */
    ALREADY_RUNNING,
    /** Fewer pages than the shape reads from, or more than one turn reads. */
    WRONG_PAGE_COUNT,
    /**
     * A page the person pointed at has closed since. The composer keeps its
     * chip so they can see which, and starts nothing until it is removed or
     * the page is opened again: a start that quietly read one page fewer
     * than the consent named would be a consent for something else.
     */
    PAGE_CLOSED,
}

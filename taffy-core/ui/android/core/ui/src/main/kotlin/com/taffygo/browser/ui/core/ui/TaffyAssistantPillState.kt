// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.ui

/**
 * Every state the assistant pill draws: the four of the handoff
 * (`handoff/TgAssistantPill.dc.html`) and the five more the bar needs to say the
 * same things on the one 48 dp chassis instead of on a second surface.
 *
 * The set is total on purpose. The bar's projection answers one of these for
 * every task the machine can produce, so no state falls through to another
 * layout — and what used to fall through was a bordered card holding a chip, a
 * line and a control bar, drawn into a 56 dp row and clipped to it.
 */
enum class TaffyAssistantPillState {
    /** Nothing is running: "Ask Taffy" and the ink orb. */
    IDLE,

    /** Taffy is working: a status line, the progress rail, and Pause. */
    RUNNING,

    /** Taffy needs something only the user can give it: solid accent, Review. */
    WAITING,

    /** The person held it and this revision admits the resume: outline, Resume. */
    PAUSED,

    /**
     * A task that exists while nothing is moving it.
     *
     * The outline pill with no rail and no control. Two moments reach it and
     * both are a claim the pill must not make: a task nothing is driving, where
     * a growing rail would say work is under way immediately after the line
     * said it is not; and a held task whose revision does not admit RESUME,
     * where a Resume chip would offer a control the reducer refuses. Neither is
     * a state the task machine has a word for, so this one has no word either —
     * the line carries the whole of what is true.
     */
    HELD,

    /** A validated complete result: the positive wash, and the check. */
    DONE,

    /** A result with labelled gaps: the accent wash, and the half circle. */
    PARTLY_DONE,

    /** What the user did: the sunken pill and the stop mark. */
    STOPPED,

    /** What happened to Taffy: the danger wash and the warning mark. */
    FAILED,
}

// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import com.taffygo.browser.ui.core.task.CoreUiAvailability
import com.taffygo.browser.ui.core.task.TaffyReadiness

/**
 * What the world looks like from the box, which is not the box's own state.
 *
 * The start rule needs four facts the person did not type: whether Taffy can
 * reach a provider, whether the core is up, whether a task is already under
 * way, and whether the tab under the box is a blank one. They are kept apart
 * from the draft because a draft is spent when the box is left and these are
 * not — the reducer empties the words and carries the conditions over, and a
 * box that forgot the core was ready every time it was emptied would refuse
 * the next start until the next publication happened to arrive.
 *
 * Every default is the guarded one: an unknown readiness refuses a start with
 * "not ready yet", not with a task.
 */
data class StartConditions(
    /** Whether a request would reach a provider, and by which route. */
    val readiness: TaffyReadiness = TaffyReadiness.Unknown,
    /** Whether the core is ready to admit a command. */
    val availability: CoreUiAvailability = CoreUiAvailability.STARTING,
    /** Whether a task is under way already; one at a time is the rule. */
    val taskAlreadyRunning: Boolean = false,
    /**
     * Whether the selected tab has been nowhere.
     *
     * A question typed over a page is about that page and opens the Ask sheet
     * with the page attached. The same question on a blank tab has no page to
     * be about, so it is an errand — Taffy finds the site itself — and starts
     * where it was typed.
     */
    val onBlankTab: Boolean = false,
    /**
     * Whether this box is the Ask overlay's, standing over a page, rather
     * than the start page's or screen SCR-103's.
     *
     * The overlay is the one host on which every reading is a job for
     * Taffy: a person who tapped Ask Taffy and typed an address has asked
     * Taffy about it, not asked to go there. It is also the host where a
     * research shape starts in place — the pages it reads are the chips
     * under the box — and where the box stays for the next question once a
     * task has answered (decision 0137).
     */
    val asksInPlace: Boolean = false,
)

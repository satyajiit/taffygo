// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import com.taffygo.browser.ui.core.model.AddressBarInterpretation
import com.taffygo.browser.ui.core.model.TabId
import com.taffygo.browser.ui.core.model.TaskAttachedStore
import com.taffygo.browser.ui.core.model.TaskTemplate

/** Everything the box can be asked to do, on SCR-103 and on the start page. */
sealed interface AddressBarIntent {
    data class OpenSavedFlow(val review: com.taffygo.browser.ui.core.model.SavedFlowReview) : AddressBarIntent
    data object DismissSavedFlows : AddressBarIntent


    /** The user typed. */
    data class InputChanged(val input: String) : AddressBarIntent

    /** The user chose a reading, either the top one or a row. */
    data class Choose(val interpretation: AddressBarInterpretation) : AddressBarIntent

    /** Start one person-visible microphone session. */
    data object StartVoiceInput : AddressBarIntent

    /** Stop microphone use or dismiss its review/error overlay. */
    data object CancelVoiceInput : AddressBarIntent

    /** Put the reviewed transcript in the box without committing its reading. */
    data object ConfirmVoiceInput : AddressBarIntent

    /** Open the options the plus carries, over the box. */
    data object OpenMenu : AddressBarIntent

    /** Close them again, having chosen something or nothing. */
    data object DismissMenu : AddressBarIntent

    /**
     * State the shape of the job, from the plus.
     *
     * It closes the menu and draws a chip; it starts nothing. What it changes is
     * what the box means — see [AddressBarUiState.reading].
     */
    data class ChooseShape(val template: TaskTemplate) : AddressBarIntent

    /** Take the chip off again; the box goes back to meaning what it read. */
    data object ClearShape : AddressBarIntent

    /**
     * Attach a store to the request, or take it off again — from the plus, or
     * from the chip it drew.
     *
     * Like [ChooseShape] it closes the menu and starts nothing. What it changes
     * is what Taffy is handed when the person sends: the store's tools
     * (decision 0133), which the consent line under the reading names.
     */
    data class ToggleStore(val store: TaskAttachedStore) : AddressBarIntent

    /**
     * Take a page off the question, from its chip.
     *
     * The chip row is the consent for what Taffy will read, so a page taken
     * off it is a page Taffy is not sent; the reading under the box changes
     * with it, from a question over pages to one over none.
     */
    data class RemovePage(val tabId: TabId) : AddressBarIntent

    /** Open the Add pages sheet over the box, from the plus. */
    data object OpenAttachPages : AddressBarIntent

    /** Close it having chosen nothing. */
    data object DismissAttachPages : AddressBarIntent

    /**
     * Close the Add pages sheet with these tabs ticked: they become the
     * pages on the question, and a page that has closed since it was
     * attached stays on it in caution until the person takes it off.
     */
    data class ConfirmAttachPages(val tabIds: List<TabId>) : AddressBarIntent

    /**
     * Ask the core to start again, from the row that says it is not ready.
     *
     * The row is the one refusal a person can act on without leaving the
     * box when the core is the thing in the way; it changes nothing on
     * screen by itself, and the conditions the start rule reads move when
     * the core publishes.
     */
    data object RetryCore : AddressBarIntent

    /**
     * Send the same request again, once the task the box started has ended.
     *
     * The words, the shape and the stores are still in the box — a task that
     * failed or was stopped leaves them where the person put them — so this
     * is [Choose] of the reading the box shows, pressed on the panel that
     * stands in the box's place: the ended task gives that place to the new
     * one. The start rule answers afresh, so a provider connected since the
     * task ended is read now, and nothing is retyped.
     */
    data object TryAgain : AddressBarIntent

    /**
     * Leave the ended task and go home: the panel goes, the box empties, and
     * the start page comes back whole.
     *
     * The words go with the panel. A person who wants the same request again
     * has [TryAgain]; this is the other door, and it leads to a start page
     * with nothing on it rather than to a box holding a request that has
     * already ended once. It navigates nowhere and stops nothing, because an
     * ended task has nothing left to stop.
     */
    data object LeaveTask : AddressBarIntent

    /** The user gave up on the box. */
    data object Dismiss : AddressBarIntent

    /**
     * The box went off screen, on a host that stays.
     *
     * Screen SCR-103 is left by [Dismiss] or by committing, and its whole
     * back-stack entry is released with it — so what was typed there cannot
     * reach the next box. The start page's composer has no such release:
     * SCR-101 is the bottom of the stack and never leaves it, so its entry, its
     * view model and its saved draft outlive every start body drawn on it.
     *
     * Without this, a draft abandoned by tapping a frequent tile would be
     * waiting in the next empty tab's box, offering a reading of an address
     * typed minutes ago and joining the next thing typed onto the end of it.
     * That is the defect [reduceAddressBar] documents at length, reached by the
     * one road that record did not have to consider.
     *
     * It navigates nowhere, which is the whole reason it is not [Dismiss].
     */
    data object Left : AddressBarIntent
}

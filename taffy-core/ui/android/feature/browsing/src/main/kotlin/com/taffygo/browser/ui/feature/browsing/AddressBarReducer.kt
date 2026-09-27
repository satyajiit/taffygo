// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import com.taffygo.browser.ui.core.model.AddressBarInterpretation
import com.taffygo.browser.ui.core.model.AddressBarCommand
import com.taffygo.browser.ui.core.model.Suggestion
import com.taffygo.browser.ui.core.task.TaskStartDecision
import com.taffygo.browser.ui.core.task.TaskStartRefusal
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.VoiceEntryState

/**
 * What typing does to screen SCR-103.
 *
 * The resolution is not made here: the data layer owns the rule, and this
 * function only places the answer into the state. Keeping the two apart is what
 * stops a second, subtly different resolver appearing in a screen.
 *
 * ## A draft lives exactly as long as the box that holds it
 *
 * Choosing a reading and giving up both leave screen SCR-103, and what was
 * typed stops being true the moment either happens. It used to survive: both
 * returned the state unchanged, the view model
 * saved that state's input, and the next box opened on it. On a phone that read
 * as two faults at once. The reopened box offered a reading of an address typed
 * minutes earlier — one tap on the top row and the person left the page they
 * were reading, for somewhere they had asked for on a different tab. And
 * because the field opened with text already in it, the next thing typed joined
 * onto the end of that text rather than replacing it, which is where
 * "wikipedia.orgwhat is a tapir" came from.
 *
 * So a box that is left empties. That is the whole fix, and it is here rather
 * than in the view model because a state rule that lives in a view model is a
 * state rule no host test can reach.
 */
internal fun reduceAddressBar(
    state: AddressBarUiState,
    intent: AddressBarIntent,
    resolve: (String) -> AddressBarInterpretation,
    suggest: (String) -> List<Suggestion>,
): AddressBarUiState = when (intent) {
    is AddressBarIntent.OpenSavedFlow, AddressBarIntent.DismissSavedFlows -> state
    is AddressBarIntent.InputChanged ->
        addressBarShowing(intent.input, resolve, suggest).keeping(state)
    AddressBarIntent.StartVoiceInput -> state.copy(
        voiceEntry = VoiceEntryState.RequestingPermission,
    )
    AddressBarIntent.CancelVoiceInput -> state.copy(voiceEntry = VoiceEntryState.Closed)
    AddressBarIntent.ConfirmVoiceInput -> {
        val reviewed = state.voiceEntry as? VoiceEntryState.Review
        if (reviewed == null) {
            state
        } else {
            addressBarShowing(reviewed.transcript.text, resolve, suggest).keeping(state)
        }
    }
    AddressBarIntent.OpenMenu -> state.copy(menuOpen = true)
    AddressBarIntent.DismissMenu -> state.copy(menuOpen = false)
    // Stating a shape closes the menu it was stated from, because the menu is
    // one choice deep and standing open over an answered question is a menu
    // asking it again.
    is AddressBarIntent.ChooseShape ->
        state.copy(shape = intent.template, menuOpen = false)
    AddressBarIntent.ClearShape -> state.copy(shape = null)
    // A page comes off the question from its chip; the reading under the
    // box follows, because the pages are what decide its shape.
    is AddressBarIntent.RemovePage -> state.copy(
        attachedPages = state.attachedPages.filterNot { it.tabId == intent.tabId },
    )
    AddressBarIntent.OpenAttachPages -> state.copy(attachOpen = true, menuOpen = false)
    AddressBarIntent.DismissAttachPages -> state.copy(attachOpen = false)
    // The ticked tabs become the pages on the question; a page that has
    // closed since it was attached stays, in caution, until it is taken off.
    is AddressBarIntent.ConfirmAttachPages -> state.copy(
        attachedPages = pagesAfterAttachConfirm(
            eligible = state.pages.eligibleTabs,
            ticked = intent.tabIds,
            previously = state.attachedPages,
        ),
        attachOpen = false,
    )
    // Asking the core to start again changes nothing here: the conditions
    // move when it publishes.
    AddressBarIntent.RetryCore -> state
    // A store is a toggle: the same tap from the plus attaches it and takes it
    // off, and the chip's own remove is the second of those.
    is AddressBarIntent.ToggleStore -> state.copy(
        attachedStores = if (intent.store in state.attachedStores) {
            state.attachedStores - intent.store
        } else {
            state.attachedStores + intent.store
        },
        menuOpen = false,
    )
    // A reading that starts here keeps its words: they are what the panel
    // under the box is about, and the person has not left. The menu and the
    // microphone close, because sending is the end of composing. A reading the
    // start rule refused keeps its words too — the row under the box says why,
    // and a box that emptied itself while saying "set up Taffy first" would be
    // asking the person to type the request twice.
    is AddressBarIntent.Choose -> when {
        state.startsHere(intent.interpretation) -> state.copy(
            starting = true,
            startFailure = null,
            menuOpen = false,
            voiceEntry = VoiceEntryState.Closed,
        )
        state.refusedHere(intent.interpretation) -> state.copy(menuOpen = false)
        else -> state.emptied(resolve, suggest)
    }
    // The same request again, from the panel of the task that ended. It is a
    // second `Choose` of the reading the box still shows, and the start rule
    // answers it afresh: the task that ended no longer counts as one running,
    // and a provider connected since is read now rather than then. Refused,
    // the box comes back with the row under it saying why. On the Ask
    // overlay the box emptied itself when the task was admitted, so the
    // request comes back into it first — the words are the started task's
    // goal, which is what "the same request" means there.
    AddressBarIntent.TryAgain -> {
        val started = state.started
        val shown = if (started != null && state.input.isBlank()) {
            addressBarShowing(started.goal, resolve, suggest).keeping(state)
        } else {
            state
        }
        when {
            started == null -> state
            shown.reading?.let(shown::startsHere) == true -> shown.copy(
                started = null,
                starting = true,
                startFailure = null,
            )
            // A task still under way is not one to send again. The panel offers
            // the door only once the task has ended, so a press that reaches
            // here anyway changes nothing rather than taking the panel down from
            // under a task that is still working.
            (shown.start as? TaskStartDecision.Refused)?.reason == TaskStartRefusal.ALREADY_RUNNING ->
                state
            else -> shown.copy(started = null)
        }
    }
    // Going home from an ended task is leaving: the panel and the words go
    // together, and the start page comes back whole.
    AddressBarIntent.LeaveTask -> state.emptied(resolve, suggest)
    // Choosing elsewhere commits and dismissing is giving up: both leave, and
    // neither leaves a draft behind. What `state` held is deliberately not carried over — carrying it
    // is the defect. The chip goes with the words for the same reason: it was
    // an option on *this* question, and the next question has not been asked
    // yet.
    AddressBarIntent.Dismiss,
    AddressBarIntent.Left,
    -> state.emptied(resolve, suggest)
}

/**
 * The box with nothing in it, standing in the same world.
 *
 * The conditions and the open tabs are the two things carried over: they are
 * facts about the core and the browser rather than about the draft, and a box
 * that forgot the core was ready every time it was emptied would refuse the
 * next start until the next publication happened to arrive.
 */
private fun AddressBarUiState.emptied(
    resolve: (String) -> AddressBarInterpretation,
    suggest: (String) -> List<Suggestion>,
): AddressBarUiState = addressBarShowing(
    input = NOTHING_TYPED,
    resolve = resolve,
    suggest = suggest,
).copy(conditions = conditions, pages = pages)

/**
 * The projection of new text, carrying over what is not about the text.
 *
 * [addressBarShowing] answers "what does this input show", and a state built
 * from it holds nothing else — which is right for a box being emptied and wrong
 * for a box being typed into: a chip the person set before they started typing
 * — a shape, a store, a page — would come off on the first keystroke, and
 * again on every one after it. The task the box started stays too, because
 * on the Ask overlay the box is typed into again under the task's answer,
 * for the next question (decision 0137). What is not carried is the failure
 * of the last send: it is said under the box until the next keystroke.
 */
internal fun AddressBarUiState.keeping(previous: AddressBarUiState): AddressBarUiState = copy(
    shape = previous.shape,
    attachedStores = previous.attachedStores,
    conditions = previous.conditions,
    attachedPages = previous.attachedPages,
    pages = previous.pages,
    attachOpen = previous.attachOpen,
    started = previous.started,
    starting = previous.starting,
)

/**
 * What the box shows for one piece of input: the text itself, what it means,
 * and the rows underneath.
 *
 * Typing arrives here, and so does opening the box — [AddressBarViewModel]
 * restores a draft through this function rather than projecting it a second
 * time by hand. One place decides what an empty box looks like, so an empty box
 * cannot come back different from the way the reducer emptied it.
 */
internal fun addressBarShowing(
    input: String,
    resolve: (String) -> AddressBarInterpretation,
    suggest: (String) -> List<Suggestion>,
): AddressBarUiState = AddressBarUiState(
    input = input,
    interpretation = input.takeIf { it.isNotBlank() }?.let(resolve),
    suggestions = suggest(input),
)

/**
 * Where a chosen reading goes when it does not start here, and what it takes
 * with it.
 *
 * A pure function rather than a branch inside the view model, because it is the
 * one place the reading a person picked becomes the screen they land on — and
 * because "the overlay opens carrying what was typed" is a claim a host test
 * should be able to make without a navigator, a browser or a phone.
 *
 * Two content readings carry what was typed, and both carry it to the Ask
 * overlay. A task in a research shape reads named pages, and the overlay is
 * where pages are named: it opens with the question and the shape stated, so
 * the chip a person set from the plus arrives as the overlay's own chip. A
 * question over a page opens the same overlay with that page attached. Neither
 * reaches this function when it starts where it was typed — an errand, or a
 * question on a blank tab — because [AddressBarUiState.start] answers those
 * first.
 *
 * The question reading once named a bare Assistant bar and dropped the
 * question on the floor: nothing failed and nothing was empty — SCR-301 opened
 * on whatever the assistant was already doing, so the answer to "why did it
 * ignore me" was that it was never told. Handing the words to the destination
 * is the mechanism [TaffyDestination.arguments] exists for, and it is what
 * makes them survive process death without the screen holding them.
 *
 * The remaining content readings need no argument: both commit to the browser
 * first. A browser command names an existing destination directly.
 */
internal fun addressBarDestination(interpretation: AddressBarInterpretation): TaffyDestination =
    when (interpretation) {
        is AddressBarInterpretation.TaskForTaffy -> TaffyDestination.AssistantBar(
            question = interpretation.input,
            shape = interpretation.template,
        )
        is AddressBarInterpretation.AskTaffy ->
            TaffyDestination.AssistantBar(question = interpretation.input)
        is AddressBarInterpretation.BrowserCommand -> commandDestination(interpretation.command)
        is AddressBarInterpretation.GoTo,
        is AddressBarInterpretation.Search,
        -> TaffyDestination.BrowserMain
    }

private fun commandDestination(command: AddressBarCommand): TaffyDestination = when (command) {
    AddressBarCommand.OPEN_HISTORY -> TaffyDestination.History
    AddressBarCommand.OPEN_BOOKMARKS -> TaffyDestination.Bookmarks
    AddressBarCommand.OPEN_DOWNLOADS -> TaffyDestination.Downloads
    AddressBarCommand.OPEN_SETTINGS -> TaffyDestination.SettingsHome
    AddressBarCommand.OPEN_CLEAR_BROWSING_DATA -> TaffyDestination.ClearBrowsingData
}

/** No draft at all, which is what a box that has been left holds. */
private const val NOTHING_TYPED = ""

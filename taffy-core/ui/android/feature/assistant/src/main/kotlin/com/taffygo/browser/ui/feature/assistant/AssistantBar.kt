// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.assistant

import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import com.taffygo.browser.ui.core.model.TaskControl
import com.taffygo.browser.ui.core.model.TaskDisplayState
import com.taffygo.browser.ui.core.ui.StatusPresentation
import com.taffygo.browser.ui.core.ui.TaffyAssistantPill
import com.taffygo.browser.ui.core.ui.TaffyAssistantPillState
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyNavigator
import com.taffygo.browser.ui.core.ui.screenViewModel
import com.taffygo.browser.ui.core.ui.taffyModeLabel
import com.taffygo.browser.ui.core.ui.taffyString

/**
 * Screen SCR-301 — the Assistant bar.
 *
 * The bar **is** the assistant pill of the handoff, in every state it can be
 * in: idle, running, waiting and paused map to its states one to one, and the
 * four final states map to the pill's own treatments of the design document's
 * task-state row. It sits in the browser's own chrome and pushes rather than
 * overlays, so it is composed into the browsing surface as a slot rather than
 * drawn over it.
 *
 * **It is one 48 dp pill and never a second surface.** It used to expand into a
 * bordered card whenever anything at all was happening — a mode chip, a task
 * line, a result action or a control — and the browser draws it into an action
 * row of a fixed height, so every one of those states was clipped on a phone
 * while nothing failed.
 *
 * **It is also the only surface in the bottom chrome.** The takeover band that
 * carried the mode chip, a second status line and Take over stood one row above
 * this one and said what this says; decision 0141 removed it, so Take over is a
 * chip here, the mode is announced through this pill's own state description,
 * and there is one line about the task rather than two. Stop is one tap away on
 * the task view the pill opens (UX spec sections 3 and 6, parity row
 * PAR-A11Y-002).
 */
@Composable
fun AssistantBar(
    navigator: TaffyNavigator,
    modifier: Modifier = Modifier,
) {
    val viewModel: AssistantBarViewModel = screenViewModel(CollapsedBar)
    val state by viewModel.state.collectAsStateWithLifecycle()
    AssistantBarContent(
        state = state,
        onIntent = { viewModel.onIntent(it, navigator) },
        modifier = modifier,
    )
}

/**
 * The bar nobody asked anything, which is what the browser's own chrome draws.
 *
 * A value rather than a fresh destination per call: an ask mints an identity
 * and the identity is what scopes the Ask overlay's view model, so constructing
 * one here would rebuild the bar's view model on every recomposition of every
 * browsing frame. The collapsed bar is one thing that is always there and it
 * has one identity, which is exactly what `TaffyDestination.AssistantBar()`
 * with no question is. An ask with a question never reaches this composable:
 * it presents over the screen beneath, and the shell draws the overlay for it.
 */
internal val CollapsedBar = TaffyDestination.AssistantBar()

/** The stateless half. */
@Composable
fun AssistantBarContent(
    state: AssistantBarUiState,
    onIntent: (AssistantBarIntent) -> Unit,
    modifier: Modifier = Modifier,
) {
    val line = assistantLine(state)
    val pillState = pillStateOf(state)
    TaffyAssistantPill(
        state = pillState,
        label = line,
        // A finished task's pill is its own control: the spec gives done and
        // partly done "open results" and there is no chip to hang it on, since
        // a chip wide enough to say so leaves no room for the line it would sit
        // beside. Every other state opens the assistant.
        onClick = {
            if (hasResults(state.state)) {
                onIntent(AssistantBarIntent.OpenResults)
            } else {
                onIntent(AssistantBarIntent.OpenAssistant)
            }
        },
        onAction = when (pillState) {
            TaffyAssistantPillState.WAITING -> {
                { onIntent(AssistantBarIntent.ReviewApproval) }
            }
            // Resume is the paused pill's own control in the handoff, and it is
            // one here because the reducer admits it on the bar: a provider
            // limit and going offline pause a task rather than failing it, and
            // the way back from both is a resume this chip performs rather than
            // navigates to.
            TaffyAssistantPillState.PAUSED -> {
                { onIntent(AssistantBarIntent.Control(TaskControl.RESUME)) }
            }
            else -> null
        },
        labelModifier = Modifier
            .testTag(ASSISTANT_LINE_TEST_TAG)
            .semantics { contentDescription = line },
        // Take over, where this exact revision admits it. It stood on the
        // takeover band until decision 0141 took that band away: the band said
        // what the pill says, one row higher, and a person reaching for the one
        // control that gives them the page back had to find it on the surface
        // that was not the bar.
        onTakeOver = if (TaskControl.TAKE_OVER in state.controls) {
            { onIntent(AssistantBarIntent.Control(TaskControl.TAKE_OVER)) }
        } else {
            null
        },
        stateWord = assistantStateWord(state),
        modifier = modifier
            .fillMaxWidth()
            .testTag(ASSISTANT_BAR_TEST_TAG),
    )
}

/**
 * The pill state for every bar state there is.
 *
 * Total, and that is the point. It used to answer null for the four final
 * states and for a task nothing is driving, and null meant "draw something
 * else" — a chip, a line and a button in a column the action row had no height
 * for. There is nothing else to draw now, so there is nothing for a new task
 * state to fall through to.
 */
internal fun pillStateOf(state: AssistantBarUiState): TaffyAssistantPillState = when {
    // The running pill draws a growing rail, which is motion saying work is
    // under way. Under a line that says nothing is working on this task, that
    // rail is the claim the words just withdrew — so a task nothing is driving
    // takes the held pill, which has no rail and no control.
    state.speaksNotice -> TaffyAssistantPillState.HELD
    else -> when (state.state) {
        null -> TaffyAssistantPillState.IDLE
        TaskDisplayState.RUNNING -> TaffyAssistantPillState.RUNNING
        TaskDisplayState.WAITING_FOR_YOU -> TaffyAssistantPillState.WAITING
        // The outline pill with Resume, but only where this exact revision
        // admits the resume — the control list is the reducer's answer and the
        // bar never guesses one from the phase (parity row PAR-A11Y-002). Where
        // it does not, the same outline pill without the chip.
        TaskDisplayState.PAUSED ->
            if (TaskControl.RESUME in state.controls) {
                TaffyAssistantPillState.PAUSED
            } else {
                TaffyAssistantPillState.HELD
            }
        TaskDisplayState.DONE -> TaffyAssistantPillState.DONE
        TaskDisplayState.PARTLY_DONE -> TaffyAssistantPillState.PARTLY_DONE
        TaskDisplayState.STOPPED -> TaffyAssistantPillState.STOPPED
        TaskDisplayState.FAILED -> TaffyAssistantPillState.FAILED
    }
}

/**
 * The two states UX spec section 4 gives the "open results" control: done, and
 * partly done.
 *
 * Partly done keeps it, because partial work is still work and hiding its
 * results would round the failure up into nothing. Stopped and failed do not
 * get it — the spec gives them other controls — and neither does a task still
 * under way, whose surface is the task view.
 */
private fun hasResults(state: TaskDisplayState?): Boolean =
    state == TaskDisplayState.DONE || state == TaskDisplayState.PARTLY_DONE

/**
 * The mode and the state, in that order, as one announced phrase.
 *
 * Two obligations meet here. UX spec section 12.1 wants the state's own word
 * announced, and section 3 wants every mode change announced — the mode chip
 * met the second until decision 0141 removed the band it stood on, and the
 * bottom bar is now one pill with one line, which has no room for a chip and
 * still has to say both. Null exactly where the line has just withdrawn the
 * claim: a task nothing is driving is still "Running" to the task machine, and
 * repeating that after the line has said otherwise is the contradiction the
 * held pill exists to avoid.
 */
@Composable
private fun assistantStateWord(state: AssistantBarUiState): String? {
    val display = state.state?.takeUnless { state.speaksNotice } ?: return null
    val word = taffyString(StatusPresentation.of(display).labelRes)
    if (!state.mode.showsChip) return word
    return taffyString(
        R.string.taffy_assistant_mode_state,
        taffyString(taffyModeLabel(state.mode)),
        word,
    )
}

/** The tags screen SCR-301's semantics tests name. */
const val ASSISTANT_BAR_TEST_TAG: String = "assistant_bar"
const val ASSISTANT_LINE_TEST_TAG: String = "assistant_bar_line"

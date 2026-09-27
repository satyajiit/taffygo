// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxScope
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.widthIn
import androidx.compose.material3.Icon
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.input.pointer.PointerEventPass
import androidx.compose.ui.input.pointer.PointerEventType
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.LiveRegionMode
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.liveRegion
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import com.taffygo.browser.ui.core.designsystem.TaffyBorders
import com.taffygo.browser.ui.core.designsystem.TaffyOpacity
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.designsystem.taffyFlash
import com.taffygo.browser.ui.core.ui.TaffyIcon
import com.taffygo.browser.ui.core.ui.taffyString

/**
 * The page held while Taffy is working it — and silent until somebody touches
 * it.
 *
 * A task drives the page by pointing the engine at addresses and at widgets it
 * has read. A person touching the same page at the same time is a second
 * driver: the tap lands between the read and the action it authorized, the
 * page under Taffy is no longer the page Taffy was told about, and the step
 * either fails its live-node check or succeeds against the wrong thing. So the
 * page stops taking touches for as long as Taffy is the one working it.
 *
 * **The hold is invisible until it is met.** This used to draw a veil over the
 * page and a card in the middle of it for the whole of a task, and both were
 * saying what the bottom bar says a row below — that Taffy is working, and how
 * to take the page back. A standing card over the page a person is watching
 * Taffy work is the one thing in the way of the only reason to watch it, and a
 * standing veil dims the same page for a message they have already read. The
 * frame round the page ([TakeoverFrame]) is what says whose the page is while
 * nothing is happening, because it says it at the edges.
 *
 * **So the answer is keyed to the touch that needs it.** A press wakes the veil
 * and the card at full strength, holds them long enough to be read, and fades
 * them out; a second press part-way through starts the whole thing again rather
 * than being swallowed by the one already leaving ([taffyFlash]). Nobody who
 * does not reach for the page is ever told anything, and nobody who does is
 * left wondering why it did not answer.
 *
 * **This is a hold on the page, never on the browser.** The address bar, the
 * action row and the assistant pill all stay live above it, because the two
 * ways out of the hold are on them: stop the task, or take the page over. A
 * hold with no door is a trap, and the whole point of saying who is driving is
 * that the person can take the wheel.
 *
 * It lifts of its own accord as well. Taffy handing the page back — a value
 * only the person knows, a sign-in, a puzzle — makes the task wait for them,
 * and a waiting task is not working the page, so the hold ends and the cut-out
 * of [TakeoverHighlight] takes over the job of saying where to touch. That is
 * why this composable is drawn on `showsLock` and not on `active`.
 *
 * The hold is a real one: every pointer event is consumed on the initial pass,
 * before the page host below it in [BrowserPageArea] is offered the event at
 * all. `TakeoverFrame` and `TakeoverHighlight` are siblings that deliberately
 * take no pointer; this one is the sibling that does.
 */
@Composable
internal fun TakeoverInputLock(
    canTakeOver: Boolean,
    modifier: Modifier = Modifier,
) {
    // A count rather than a flag: the alert has to restart on the second touch
    // as well as begin on the first, and a boolean that is already true says
    // nothing when it is set again.
    var touches by remember { mutableIntStateOf(0) }
    val strength = taffyFlash(
        token = touches,
        holdMillis = NoticeHoldMillis,
        // Stillness takes the fade, not the notice: a person who asked for no
        // motion still has to be told why the page did not answer them.
        fadeMillis = if (TaffyTheme.reducedMotion) 0 else NoticeFadeMillis,
    )
    Box(
        modifier = modifier
            .fillMaxSize()
            // Every event, on the initial pass, so nothing below is offered it.
            // A tap gesture detector would swallow taps and pass scrolls, and a
            // page a person can still scroll under Taffy is a page whose node
            // rectangles have moved out from under the action about to spend
            // them.
            .pointerInput(Unit) {
                awaitPointerEventScope {
                    while (true) {
                        val event = awaitPointerEvent(PointerEventPass.Initial)
                        // The press and not every change: a finger resting on
                        // the page reports a move on nearly every frame, and an
                        // alert restarted by each of them would never leave.
                        if (event.type == PointerEventType.Press) {
                            touches++
                        }
                        event.changes.forEach { it.consume() }
                    }
                }
            }
            .testTag(TAKEOVER_LOCK_TEST_TAG),
        contentAlignment = Alignment.Center,
    ) {
        // Composed only while it is being shown, so a held page costs one
        // pointer-consuming box and nothing else — and so the notice's live
        // region announces on arriving rather than once, at the start of the
        // task, for a screen reader that was reading something else.
        if (strength.value > 0f) {
            TakeoverLockNotice(canTakeOver = canTakeOver, strength = strength.value)
        }
    }
}

/**
 * The veil and the card, at whatever strength the touch has left them.
 *
 * One layer for both, so they arrive and leave together: a card fading over a
 * veil that is already gone reads as two separate things happening, and the
 * veil is what makes the card legible over an arbitrary page.
 */
@Composable
private fun BoxScope.TakeoverLockNotice(canTakeOver: Boolean, strength: Float) {
    val line = taffyString(R.string.taffy_browser_takeover_lock_line)
    val hint = if (canTakeOver) {
        taffyString(R.string.taffy_browser_takeover_lock_hint_take_over)
    } else {
        taffyString(R.string.taffy_browser_takeover_lock_hint_wait)
    }
    val spoken = taffyString(R.string.taffy_browser_takeover_description, line, hint)
    val colors = TaffyTheme.colors
    Box(
        modifier = Modifier
            .matchParentSize()
            .graphicsLayer { alpha = strength }
            .background(colors.scrim.copy(alpha = TaffyOpacity.DRIVING_LOCK))
            .testTag(TAKEOVER_NOTICE_TEST_TAG)
            .semantics {
                contentDescription = spoken
                liveRegion = LiveRegionMode.Polite
            },
        contentAlignment = Alignment.Center,
    ) {
        Column(
            modifier = Modifier
                .widthIn(max = TagMaxWidth)
                .clip(TaffyTheme.shapes.card)
                .background(colors.surfaceRaised)
                .border(TaffyBorders.standard, colors.accent, TaffyTheme.shapes.card)
                .padding(
                    horizontal = TaffyTheme.spacing.screenMargin,
                    vertical = TaffyTheme.spacing.snug,
                ),
            horizontalAlignment = Alignment.CenterHorizontally,
            verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.step),
        ) {
            Icon(
                imageVector = TaffyIcon.Sparkle,
                contentDescription = null,
                modifier = Modifier.size(TagIconSize),
                tint = colors.accent,
            )
            Text(
                text = line,
                style = TaffyTheme.typography.label,
                color = colors.textPrimary,
                textAlign = TextAlign.Center,
            )
            Text(
                text = hint,
                style = TaffyTheme.typography.caption,
                color = colors.textSecondary,
                textAlign = TextAlign.Center,
            )
        }
    }
}

/** Narrow enough that the page is still readable around the tag. */
private val TagMaxWidth = 260.dp

/** Large enough to read as the mark rather than as punctuation. */
private val TagIconSize = 20.dp

/**
 * How long the notice stands at full strength before it starts to go.
 *
 * Long enough to read two short lines after looking down at a finger that did
 * nothing — which is about a second and a half, not the third of a second a
 * toast gets for a message the person already expected.
 */
private const val NoticeHoldMillis = 1600

/** And long enough on the way out to read as leaving rather than as a glitch. */
private const val NoticeFadeMillis = 700

/** The tag the input hold's tests name. */
const val TAKEOVER_LOCK_TEST_TAG: String = "browser_takeover_lock"

/** The tag the notice that answers a touch is found by. */
const val TAKEOVER_NOTICE_TEST_TAG: String = "browser_takeover_notice"

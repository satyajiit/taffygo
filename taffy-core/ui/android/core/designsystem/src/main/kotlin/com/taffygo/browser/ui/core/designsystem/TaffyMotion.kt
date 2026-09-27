// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.designsystem

import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.State
import androidx.compose.runtime.derivedStateOf
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.withFrameMillis

/**
 * The one looping-motion primitive, driven a frame at a time.
 *
 * ## Why not `rememberInfiniteTransition`
 *
 * It lives in `compose-animation-core`, whose Chromium target restricts its
 * visibility to `//third_party/androidx:*` at the pinned milestone. Both the
 * target and its alias do, so there is no consumable form — the same shape as
 * OD-076. A composable that used it would compile in the UI layer and fail
 * `gn gen` in the fork, which is the worst place to find out and the reason
 * this exists rather than a patch widening someone else's visibility list.
 *
 * `withFrameMillis` is in `compose-runtime`, which anything that composes at
 * all already depends on.
 *
 * ## Why one primitive rather than four call sites
 *
 * Every loop in this product means the same thing — Taffy is working — and the
 * handoff's motion rule is that nothing loops in the corner of the eye unless
 * something genuinely is. A single primitive that takes `running` makes that
 * rule mechanical: pass false and no frame is ever scheduled, so the
 * reduced-motion case is exact rather than a value that happens not to change.
 */
@Composable
fun taffyPhase(running: Boolean, periodMillis: Int): State<Float> {
    val phase = remember { mutableFloatStateOf(0f) }
    LaunchedEffect(running, periodMillis) {
        if (!running || periodMillis <= 0) {
            phase.floatValue = 0f
            return@LaunchedEffect
        }
        var start = 0L
        while (true) {
            withFrameMillis { now ->
                if (start == 0L) start = now
                phase.floatValue = ((now - start) % periodMillis).toFloat() / periodMillis
            }
        }
    }
    return phase
}

/**
 * A phase that runs out and back rather than snapping, for a pulse.
 *
 * `RepeatMode.Reverse` in one function, because a value that jumps from one to
 * zero reads as a flicker and every pulse in the product wants the other thing.
 */
@Composable
fun taffyPingPongPhase(running: Boolean, periodMillis: Int): State<Float> {
    val linear = taffyPhase(running, periodMillis * 2)
    return remember(linear) {
        derivedStateOf {
            val fraction = linear.value
            if (fraction <= 0.5f) fraction * 2f else (1f - fraction) * 2f
        }
    }
}

/**
 * A run that happens once and then holds, for motion that has an ending.
 *
 * [taffyPhase] is a loop, and a loop is the wrong shape for anything that
 * concludes: the modulo that keeps a backdrop's waves seamless is the same
 * modulo that sends a finished demonstration back to its first frame while a
 * person is still looking at it. This advances from zero to one over
 * [durationMillis] and then stops. The coroutine ends when it arrives, so a
 * completed run schedules no further frames — the same guarantee `running =
 * false` gives, reached the other way.
 *
 * Held still it reports zero rather than one, exactly as [taffyPhase] does. A
 * caller that wants the finished frame while nothing is running is asking a
 * question about its own content rather than about the clock, and answers it
 * itself.
 */
@Composable
fun taffyRunOnce(running: Boolean, durationMillis: Int): State<Float> {
    val amount = remember { mutableFloatStateOf(0f) }
    LaunchedEffect(running, durationMillis) {
        if (!running || durationMillis <= 0) {
            amount.floatValue = 0f
            return@LaunchedEffect
        }
        amount.floatValue = 0f
        var start = 0L
        var arrived = 0f
        while (arrived < 1f) {
            withFrameMillis { now ->
                if (start == 0L) start = now
                arrived = ((now - start).toFloat() / durationMillis).coerceIn(0f, 1f)
                amount.floatValue = arrived
            }
        }
    }
    return amount
}

/**
 * Full at once, held, then gone — for something that answers a person's touch
 * and then gets out of the way.
 *
 * The other four primitives are driven by a state that is either true or false
 * for as long as it lasts. This one is driven by an *event*: [token] is a count
 * of the things that have happened, so a second touch part-way through a fade
 * restarts the alert from full rather than being swallowed by the one already
 * leaving. A token of zero is "nothing has happened yet" and schedules no
 * frame, which is what the first composition passes.
 *
 * It arrives instantly and leaves slowly on purpose. An alert that faded *in*
 * would be answering a touch that has already finished by the time it is
 * legible; what the person needs is for the answer to be there when they look
 * down at their own finger.
 *
 * A zero [fadeMillis] snaps away at the end of the hold and is what a caller
 * under reduced motion passes — the alert still has to be seen, so the hold is
 * not what stillness removes.
 */
@Composable
fun taffyFlash(token: Int, holdMillis: Int, fadeMillis: Int): State<Float> {
    val strength = remember { mutableFloatStateOf(0f) }
    LaunchedEffect(token, holdMillis, fadeMillis) {
        val hold = holdMillis.coerceAtLeast(0)
        val fade = fadeMillis.coerceAtLeast(0)
        if (token <= 0 || hold + fade <= 0) {
            strength.floatValue = 0f
            return@LaunchedEffect
        }
        strength.floatValue = 1f
        var start = 0L
        var gone = false
        while (!gone) {
            withFrameMillis { now ->
                if (start == 0L) start = now
                val elapsed = (now - start).toFloat()
                // The guard is the whole of what could be wrong here: a zero
                // fade divides by zero, and a NaN alpha draws a blank frame and
                // raises nothing (see [taffySegment]).
                strength.floatValue = when {
                    elapsed <= hold -> 1f
                    fade <= 0 -> 0f
                    else -> 1f - taffyEaseInOut((elapsed - hold) / fade)
                }
                gone = elapsed >= hold + fade
            }
        }
        strength.floatValue = 0f
    }
    return strength
}

/** Ease-in-out, the curve the handoff's motion table names for every loop. */
fun taffyEaseInOut(fraction: Float): Float {
    val f = fraction.coerceIn(0f, 1f)
    return if (f < 0.5f) {
        2f * f * f
    } else {
        1f - (-2f * f + 2f) * (-2f * f + 2f) / 2f
    }
}

/**
 * Ease-out: away at once, decelerating into rest.
 *
 * The curve for something that arrives. Ease-in-out is symmetric, so a body
 * moving under it is still travelling at its midpoint speed when the run ends
 * — which reads as swept away rather than as having landed. This one is at
 * roughly a quarter of its peak speed by three quarters of the way through.
 */
fun taffyEaseOut(fraction: Float): Float {
    val f = fraction.coerceIn(0f, 1f)
    val remaining = 1f - f
    return 1f - remaining * remaining * remaining
}

/**
 * Smootherstep: zero velocity *and* zero acceleration at both ends.
 *
 * [taffyEaseInOut] starts and stops with no speed but with a jolt of
 * acceleration at each end. That is invisible on a moving object and very
 * visible on a colour: a palette cross-fade under it appears to flinch as it
 * begins. This is the fifth-order curve, whose first and second derivatives are
 * both zero at 0 and 1, so a blend under it starts and finishes imperceptibly.
 */
fun taffySmootherStep(fraction: Float): Float {
    val f = fraction.coerceIn(0f, 1f)
    return f * f * f * (f * (f * 6f - 15f) + 10f)
}

/**
 * One window of a run, remapped to a full zero-to-one.
 *
 * What lets a single clock drive several phases that start and finish at
 * different moments, without a second coroutine or a second source of truth
 * about how far along the run is. `taffySegment(p, 0.1f, 1f)` is "the part of
 * the run from a tenth of the way in", as its own fraction.
 *
 * An empty or inverted window answers `0f` rather than dividing by zero. That
 * is not defensive tidiness: a `NaN` reaching a `Canvas` centre draws a blank
 * frame and raises nothing, which is the least debuggable failure this file
 * could have.
 */
fun taffySegment(fraction: Float, start: Float, end: Float): Float {
    if (end <= start) return 0f
    return ((fraction - start) / (end - start)).coerceIn(0f, 1f)
}

/**
 * A value that travels to wherever [target] moves next, and then stops.
 *
 * The shape for a change of state rather than a loop or a run that concludes:
 * [taffyRunOnce] only ever advances, and a control that collapses when a task
 * starts has to come back when it ends. It eases from wherever it currently is,
 * so a target reversed mid-flight turns around from there instead of jumping to
 * the end and starting again.
 *
 * A zero [durationMillis] arrives at once and schedules no frame, which is what
 * a caller under reduced motion passes.
 */
@Composable
fun taffyTween(target: Float, durationMillis: Int): State<Float> {
    val value = remember { mutableFloatStateOf(target) }
    LaunchedEffect(target, durationMillis) {
        val from = value.floatValue
        if (durationMillis <= 0 || from == target) {
            value.floatValue = target
            return@LaunchedEffect
        }
        var start = 0L
        var arrived = false
        while (!arrived) {
            withFrameMillis { now ->
                if (start == 0L) start = now
                val fraction = ((now - start).toFloat() / durationMillis).coerceIn(0f, 1f)
                value.floatValue = from + (target - from) * taffyEaseInOut(fraction)
                arrived = fraction >= 1f
            }
        }
    }
    return value
}

/**
 * Ease-in-out with a hold at each end, for motion that has to be read at its
 * extremes rather than merely reached them.
 *
 * A line travelling far enough to need this is a line somebody is reading, and
 * an eased turn at the far end is still a turn: the last words arrive and leave
 * in the same moment. [dwell] is the fraction of the journey spent stationary at
 * each end — the rest is the ordinary curve, compressed into what is left.
 */
fun taffyDwellEase(fraction: Float, dwell: Float): Float {
    val f = fraction.coerceIn(0f, 1f)
    val hold = dwell.coerceIn(0f, 0.49f)
    return when {
        f <= hold -> 0f
        f >= 1f - hold -> 1f
        else -> taffyEaseInOut((f - hold) / (1f - 2f * hold))
    }
}


// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.designsystem.taffyPhase
import com.taffygo.browser.ui.core.designsystem.taffySmootherStep
import com.taffygo.browser.ui.core.ui.taffyString

/**
 * The start page's welcome line, rotating through a small fixed set.
 *
 * Every line is an externalized string — the set is the product's, not a
 * feed's, and nothing here is generated or fetched. The rotation is a slow
 * dissolve rather than a slide so the page breathes instead of ticking, and
 * it carries no live region on purpose: a screen reader that announced a new
 * greeting every few seconds would be talking over whatever the person was
 * actually doing. Whichever line is up is read as the page's heading.
 *
 * The motion is [taffyPhase] driving an alpha-and-drift envelope by hand, not
 * `AnimatedContent`: `androidx.compose.animation` has no Chromium-consumable
 * form at the pinned milestone (the OD-076 shape [taffyPhase]'s own KDoc
 * records), and the one clock also makes the dissolve exact — the line fades
 * out under [taffySmootherStep], swaps at zero, and rises back in, so no
 * frame ever shows two lines fighting or a layout jumping between them.
 * "Remove animations" holds the first line still and schedules no frames.
 */
@Composable
internal fun StartPageGreeting(modifier: Modifier = Modifier) {
    val still = TaffyTheme.reducedMotion
    val phase by taffyPhase(
        running = !still,
        periodMillis = GREETING_CYCLE_MILLIS * GreetingLines.size,
    )

    // One clock across the whole rotation; each line owns an equal slice.
    val position = phase * GreetingLines.size
    val index = position.toInt().coerceAtMost(GreetingLines.size - 1)
    val within = position - index

    val visibility: Float
    val drift: Float
    when {
        still -> {
            visibility = 1f
            drift = 0f
        }
        within < FADE_FRACTION -> {
            // Arriving: rises the last few units into place as it appears.
            val arrived = taffySmootherStep(within / FADE_FRACTION)
            visibility = arrived
            drift = 1f - arrived
        }
        within > 1f - FADE_FRACTION -> {
            // Leaving: keeps rising on the same path, so the swap at zero
            // alpha reads as one continuous upward motion.
            val left = taffySmootherStep((within - (1f - FADE_FRACTION)) / FADE_FRACTION)
            visibility = 1f - left
            drift = -left
        }
        else -> {
            visibility = 1f
            drift = 0f
        }
    }

    Text(
        text = taffyString(GreetingLines[index]),
        style = TaffyTheme.typography.display,
        color = TaffyTheme.colors.textPrimary,
        textAlign = TextAlign.Center,
        modifier = modifier
            .fillMaxWidth()
            .graphicsLayer {
                alpha = visibility
                translationY = drift * GreetingDrift.toPx()
            }
            .semantics { heading() }
            .testTag(START_GREETING_TEST_TAG),
    )
}

/** The greeting, which the start page's semantics tests name. */
const val START_GREETING_TEST_TAG: String = "start_greeting"

private val GreetingLines = listOf(
    R.string.taffy_start_greeting_where_to,
    R.string.taffy_start_greeting_fresh,
    R.string.taffy_start_greeting_ask,
    R.string.taffy_start_greeting_ready,
)

// Each line holds for most of its six seconds; the dissolve is the last (and
// first) 15% of the slice, so about nine tenths of a second each way — slow
// enough to be weather rather than a ticker. The drift is small on purpose:
// the line settles, it does not travel.
private const val GREETING_CYCLE_MILLIS = 6_000
private const val FADE_FRACTION = 0.15f
private val GreetingDrift = 10.dp

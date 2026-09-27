// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import androidx.compose.foundation.Canvas
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.unit.dp
import com.taffygo.browser.ui.core.designsystem.TaffyBorders
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.designsystem.drawTaffyBorderComet
import com.taffygo.browser.ui.core.designsystem.markRibbon
import com.taffygo.browser.ui.core.designsystem.taffyPhase
import com.taffygo.browser.ui.core.model.TaskInputField
import com.taffygo.browser.ui.core.ui.taffyString

/**
 * The two things drawn over the page while a task is driving it.
 *
 * **Both are window-layer Compose and cannot be anything else.** The page is a
 * `SurfaceView`: the window resolves where that surface sits, so a
 * `graphicsLayer`, a border modifier or a clip on the composable wrapping it
 * changes nothing about the pixels the engine is drawing. What does work is
 * painting in the window *above* the surface, which is what a sibling `Canvas`
 * in the same box does — see `BrowserPageArea`, where the same fact is the
 * reason the page host is never swapped out.
 *
 * Both draws are driven by `taffyPhase` and nothing else.
 * `compose-animation-core` compiles perfectly well under Gradle and its
 * Chromium target restricts visibility to `//third_party/androidx:*`, so an
 * `InfiniteTransition` here would pass every gate on this host and fail
 * `gn gen` in the fork.
 */
@Composable
internal fun TakeoverFrame(modifier: Modifier = Modifier) {
    val reducedMotion = TaffyTheme.reducedMotion
    // False schedules no frame at all, so the reduced-motion case is exact
    // rather than a value that happens to stop changing.
    val phase = taffyPhase(running = !reducedMotion, periodMillis = FrameLapMillis)
    val accent = TaffyTheme.colors.accent
    val ribbon = TaffyTheme.colors.markRibbon
    val spoken = taffyString(R.string.taffy_browser_takeover_frame_description)
    Canvas(
        modifier = modifier
            .testTag(TAKEOVER_FRAME_TEST_TAG)
            .semantics { contentDescription = spoken },
    ) {
        val stroke = TaffyBorders.rail.toPx()
        val radius = CornerRadius(FrameCornerRadius.toPx())
        // Under reduced motion the frame is the whole of the signal, so it is
        // drawn at full strength. With motion it rests dimmer and the light
        // travelling round it carries the rest — the same total presence,
        // reached the other way.
        drawFrame(
            color = accent.copy(alpha = if (reducedMotion) 1f else RestingFrameAlpha),
            stroke = stroke,
            radius = radius,
        )
        if (!reducedMotion) {
            drawTaffyBorderComet(phase = phase.value, ribbon = ribbon, cornerRadius = radius)
        }
    }
}

/**
 * The page with everything but one rectangle dimmed.
 *
 * Four rectangles round a hole rather than a cleared layer: the punch-out has
 * to composite over a surface this process does not own, and `BlendMode.Clear`
 * inside a saved layer would clear the layer's own pixels — which are the
 * page's. Drawing the shade *around* the hole never touches what is inside it.
 *
 * Nothing here takes a pointer, so every touch still reaches the page. That is
 * the point: the person has to work the widget underneath.
 */
@Composable
internal fun TakeoverHighlight(
    highlight: TaskInputField.Highlight,
    modifier: Modifier = Modifier,
) {
    val reducedMotion = TaffyTheme.reducedMotion
    val phase = taffyPhase(running = !reducedMotion, periodMillis = HighlightPulseMillis)
    val shade = TaffyTheme.colors.scrim
    val accent = TaffyTheme.colors.accent
    val spoken = taffyString(R.string.taffy_browser_takeover_highlight_description)
    Canvas(
        modifier = modifier
            .testTag(TAKEOVER_HIGHLIGHT_TEST_TAG)
            .semantics { contentDescription = spoken },
    ) {
        if (highlight.isEmpty) return@Canvas
        val left = highlight.leftFraction * size.width
        val top = highlight.topFraction * size.height
        val right = highlight.rightFraction * size.width
        val bottom = highlight.bottomFraction * size.height

        drawRect(shade, Offset.Zero, Size(size.width, top))
        drawRect(shade, Offset(0f, bottom), Size(size.width, size.height - bottom))
        drawRect(shade, Offset(0f, top), Size(left, bottom - top))
        drawRect(shade, Offset(right, top), Size(size.width - right, bottom - top))

        val stroke = TaffyBorders.rail.toPx()
        // A pulse rather than a lap: a rectangle this small has too short a
        // perimeter for a travelling light to read as anything but a flicker.
        val strength = if (reducedMotion) {
            1f
        } else {
            PulseFloor + (1f - PulseFloor) * triangle(phase.value)
        }
        drawRect(
            color = accent.copy(alpha = strength),
            topLeft = Offset(left - stroke / 2f, top - stroke / 2f),
            size = Size(right - left + stroke, bottom - top + stroke),
            style = Stroke(width = stroke),
        )
    }
}

/** The resting outline, inset half its width so its outer edge is not shaved. */
private fun DrawScope.drawFrame(color: Color, stroke: Float, radius: CornerRadius) {
    val inset = stroke / 2f
    drawRoundRect(
        color = color,
        topLeft = Offset(inset, inset),
        size = Size(size.width - stroke, size.height - stroke),
        cornerRadius = radius,
        style = Stroke(width = stroke),
    )
}

/** Out and back over one lap, so the pulse never snaps from full to nothing. */
private fun triangle(phase: Float): Float {
    val f = phase.coerceIn(0f, 1f)
    return if (f <= 0.5f) f * 2f else (1f - f) * 2f
}

private val FrameCornerRadius = 4.dp
private const val FrameLapMillis = 4200
private const val HighlightPulseMillis = 1600
private const val RestingFrameAlpha = 0.45f
private const val PulseFloor = 0.55f

/** The tags the takeover overlay's tests name. */
const val TAKEOVER_FRAME_TEST_TAG: String = "browser_takeover_frame"
const val TAKEOVER_HIGHLIGHT_TEST_TAG: String = "browser_takeover_highlight"

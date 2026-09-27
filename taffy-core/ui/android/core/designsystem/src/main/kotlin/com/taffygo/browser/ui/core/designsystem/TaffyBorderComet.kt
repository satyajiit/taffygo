// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.designsystem

import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Rect
import androidx.compose.ui.geometry.RoundRect
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.lerp
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.PathMeasure
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp

/**
 * The travelling light on a border.
 *
 * The light is a short arc of the outline itself, walked at a constant speed:
 * [phase] is how far round the *edge* the head has travelled, and the tail
 * fades over [tailSpan] of the perimeter behind it. The head stays a hard
 * edge, so it reads against the resting outline rather than dissolving into
 * it.
 *
 * ## Why the perimeter and not a sweep
 *
 * This was a sweep gradient once, and a sweep advances its stops by *angle
 * about the centre*. On a shape near a square that is close enough to arc
 * length to pass; on a bar the width of the screen and 48 dp tall it is not.
 * The head crawled the two long edges and whipped round the two short ends,
 * and because the tail was a fraction of the angle rather than of the
 * distance, its length pulsed as it went. A sweep also has a seam at twelve
 * o'clock that has to be hidden by making the colour at zero equal the colour
 * at one; walking the path has no seam to hide, because the wrap is two draws
 * rather than one gradient.
 *
 * The cost is a handful of short stroked arcs per frame instead of one
 * rounded rectangle. That is the price of the motion being even, and it is
 * paid only while something is actually moving: callers drive [phase] with
 * [taffyPhase] and skip this draw under reduced motion, so no frame is
 * scheduled then.
 *
 * ## The radius
 *
 * [cornerRadius] defaults to the pill this was written for — half the height,
 * so the ends are semicircles. **A caller whose surface is not a pill passes
 * that surface's own radius**, from [TaffyRadii], and a caller that forgets
 * draws a pill around a rounded rectangle: 13 dp of error at every corner of
 * a 48 dp row, half of it shaved flat by the clip it is drawn inside. That is
 * not hypothetical — it is what the tab switcher's Ask Taffy bar did.
 *
 * The stroke rides the resting outline, inset half its width so the clip
 * cannot shave its outer edge flat.
 *
 * ## The colour is a ramp, and the ramp is the mark
 *
 * [ribbon] is sampled across the light itself — the head takes the first stop
 * and the dim end of the tail the last — so what travels the border is a short
 * length of the TaffyGo ribbon rather than a stroke of one colour that happens
 * to move. `TaffyColors.markRibbon` is that ramp and is what nearly every
 * caller passes.
 *
 * Sampling the *tail* rather than the perimeter is deliberate: a ramp laid
 * round the border would have a seam where its last stop meets its first, and
 * the light would drag that seam past the eye once a lap. A ramp bounded by
 * the tail has no seam to hide, for the same reason walking the path has none.
 *
 * A caller whose ground is already one of those hues passes a single-colour
 * list, and gets exactly the old behaviour.
 */
fun DrawScope.drawTaffyBorderComet(
    phase: Float,
    ribbon: List<Color>,
    tailSpan: Float = DefaultCometTailSpan,
    cornerRadius: CornerRadius? = null,
) {
    val stroke = TaffyBorders.rail.toPx()
    val inset = stroke / 2f
    val width = size.width - stroke
    val height = size.height - stroke
    // A surface too small to hold its own outline has no border to travel.
    if (width <= 0f || height <= 0f) return

    val outline = Path().apply {
        addRoundRect(
            RoundRect(
                rect = Rect(Offset(inset, inset), Size(width, height)),
                cornerRadius = clampedRadius(cornerRadius, width, height),
            ),
        )
    }
    val measure = PathMeasure().apply { setPath(outline, forceClosed = true) }
    val perimeter = measure.length
    if (perimeter <= 0f) return

    val head = phase.coerceIn(0f, 1f) * perimeter
    val tail = tailSpan.coerceIn(MinimumTailSpan, 1f) * perimeter
    // Step length rather than step count, so the fade is as smooth on a wide
    // bar as on a small pill instead of being stretched across the difference.
    val steps = (tail / StepLength.toPx()).toInt().coerceIn(MinimumSteps, MaximumSteps)
    val step = tail / steps

    val arc = Path()
    val rail = Stroke(width = stroke)
    // The last step is the far end of the ramp, so the divisor is one less than
    // the count — with `steps` as the divisor the tail would stop short of the
    // final hue and the ramp would be silently cropped by however coarse the
    // fade happened to be.
    val lastStep = (steps - 1).coerceAtLeast(1)
    for (index in 0 until steps) {
        val brightness = (steps - index).toFloat() / steps
        val ink = taffyRibbonAt(ribbon, index.toFloat() / lastStep)
        val leading = head - index * step
        arc.reset()
        appendArc(measure, arc, from = leading - step, to = leading, perimeter = perimeter)
        drawPath(
            path = arc,
            color = ink.copy(alpha = ink.alpha * brightness),
            style = rail,
        )
    }
}

/**
 * One colour from a ramp, at a fraction of the way along it.
 *
 * Split out for the reason [cometArcSpans] is: it is arithmetic a host can run,
 * where the draw around it is an Android graphics object. Two edges it has to
 * get right — an empty ramp draws nothing rather than throwing, and a
 * one-colour ramp is that colour everywhere rather than a division by zero,
 * which is what a caller on a ground of its own hue passes.
 */
internal fun taffyRibbonAt(ribbon: List<Color>, at: Float): Color {
    val stops = ribbon.size
    if (stops == 0) return Color.Transparent
    if (stops == 1) return ribbon[0]
    val fraction = at.coerceIn(0f, 1f)
    val span = 1f / (stops - 1)
    val lower = (fraction / span).toInt().coerceIn(0, stops - 2)
    return lerp(ribbon[lower], ribbon[lower + 1], (fraction - lower * span) / span)
}

/**
 * One arc of the outline, split where it crosses the path's own start.
 *
 * The two halves are appended with their own `moveTo`, so a light crossing the
 * start point is two strokes that meet rather than one stroke that runs the
 * wrong way round the shape to join itself up.
 */
private fun appendArc(
    measure: PathMeasure,
    into: Path,
    from: Float,
    to: Float,
    perimeter: Float,
) {
    val (first, second) = cometArcSpans(from, to, perimeter)
    measure.getSegment(first.first, first.second, into, true)
    second?.let { measure.getSegment(it.first, it.second, into, true) }
}

/**
 * Where one step of the tail lies along the perimeter, as one span or two.
 *
 * Split out because it is the whole of what can be wrong here and the only
 * part a host can run: `PathMeasure` is an Android graphics object, so the
 * draw itself is device evidence, while this is arithmetic. Two things it has
 * to get right. The head walks backwards through zero on every lap, so `%`
 * would hand back a negative distance — which measures nothing and silently
 * draws no arc — and `mod` is what answers a position instead. And a step
 * straddling the path's start point is two spans, because a single span from
 * a high distance to a low one is the rest of the shape rather than the short
 * way across the seam.
 */
internal fun cometArcSpans(
    from: Float,
    to: Float,
    perimeter: Float,
): Pair<Pair<Float, Float>, Pair<Float, Float>?> {
    val start = from.mod(perimeter)
    val stop = start + (to - from)
    return if (stop <= perimeter) {
        (start to stop) to null
    } else {
        (start to perimeter) to (0f to stop - perimeter)
    }
}

/**
 * The radius the outline is actually drawn with.
 *
 * A radius wider than half the shape is not an error a caller can see — Skia
 * quietly scales it and the drawn corner stops matching the token that was
 * passed. Clamping here keeps the walked path and the clipped surface the same
 * shape, which is the whole point of taking a radius at all.
 */
private fun clampedRadius(requested: CornerRadius?, width: Float, height: Float): CornerRadius {
    val radius = requested ?: CornerRadius(height / 2f)
    return CornerRadius(
        x = radius.x.coerceIn(0f, width / 2f),
        y = radius.y.coerceIn(0f, height / 2f),
    )
}

/** About a quarter of the lap: a shooting light, not a spinner. */
const val DefaultCometTailSpan: Float = 0.24f

/** Below this the tail is shorter than the stroke and the head is all there is. */
private const val MinimumTailSpan: Float = 0.01f

/** How long one step of the fade is. Small enough that the banding is invisible. */
private val StepLength: Dp = 4.dp

private const val MinimumSteps: Int = 10
private const val MaximumSteps: Int = 32

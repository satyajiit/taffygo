// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.designsystem

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.key
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Rect
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.ColorFilter.Companion.tint
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.PathFillType
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.drawscope.translate
import androidx.compose.ui.graphics.vector.VectorPainter
import androidx.compose.ui.graphics.vector.rememberVectorPainter
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.semantics.clearAndSetSemantics
import com.taffygo.browser.ui.core.designsystem.internal.TaffyColorSchemes
import com.taffygo.browser.ui.core.designsystem.internal.blendTo
import kotlin.math.PI
import kotlin.math.cos
import kotlin.math.sin

/**
 * A full-window sunrise or moonrise while the application theme changes.
 *
 * ## The palette travels; nothing is covered up
 *
 * The change is a cross-fade of the token set itself: [content] is handed a
 * blend of the outgoing and incoming schemes, so every surface, word and border
 * moves from one value to the other together. An earlier version drew a flat
 * rectangle of the *outgoing* surface over a tree that had already recomposed
 * into the incoming one, which meant the old interface was never on screen at
 * all — what a person saw was the whole window vanish into a colour field and
 * come back. No duration fixes that; the palette has to be what moves.
 *
 * ## One rising body, on one clock, in four phases
 *
 * The sun or moon climbs once and holds near the top. It does not arc back
 * down, and it does not fade out at the halfway point — both of which an
 * earlier implementation did, because its vertical travel and its opacity were
 * driven by `sin(PI * progress)`, which returns to zero at both ends.
 *
 * Everything used to read [taffyEaseInOut] of the same fraction instead, which
 * fixed that and introduced the reason this reads as abrupt: **one curve drove
 * the blend, the rise and the fade, so all three began together, moved at the
 * same speed and arrived together.** The duration was never the problem. A
 * change that feels calm is layered — parts of it lead, parts follow, and the
 * thing you are watching settles before it leaves.
 *
 * So one clock still drives everything, and [taffySegment] cuts four phases out
 * of it:
 *
 * | Phase   | Curve                                        | What it buys |
 * |---------|----------------------------------------------|--------------|
 * | Palette | `taffySmootherStep` of the run after a tenth  | Colour starts a beat behind the body and settles at the very end, with no acceleration at either end. |
 * | Rise    | `taffyEaseOut` of the run up to 86%           | The body decelerates into rest and then holds still while it fades. It used to still be climbing as it disappeared, which is the single largest reason the change read as swept away rather than arrived. |
 * | Scale   | grows with the rise                           | It settles into place rather than only stopping. |
 * | Alpha   | the raw fraction                              | Read unmodified, not eased: easing it twice is what made it snap. It leaves more slowly than it arrives. |
 *
 * The duration is unchanged. `handoff/DESIGN.md` section 6 names 760 ms for
 * `themeChange`, and raising it is an edit to that table rather than a value to
 * tune here.
 *
 * Reduced motion cuts over immediately, with no blend and no sky
 * (`handoff/DESIGN.md` section 6, and parity row PAR-A11Y-004: motion is never
 * the only carrier of a state).
 */
@Composable
internal fun TaffyThemeTransition(
    darkTheme: Boolean,
    reducedMotion: Boolean,
    glyphs: TaffyThemeGlyphs? = null,
    content: @Composable (TaffyColors) -> Unit,
) {
    var previousDark by remember { mutableStateOf(darkTheme) }
    var change by remember { mutableStateOf<ThemeChange?>(null) }

    LaunchedEffect(darkTheme, reducedMotion) {
        if (darkTheme == previousDark) return@LaunchedEffect
        previousDark = darkTheme
        change = if (reducedMotion) null else ThemeChange(toDark = darkTheme)
    }

    // Keyed on the change so a second flip mid-transition restarts the run
    // rather than riding the first one's clock to its end.
    val progress by key(change) { taffyRunOnce(change != null, THEME_TRANSITION_MILLIS) }
    val transitionFinished = progress >= 1f
    LaunchedEffect(change, transitionFinished) {
        if (transitionFinished) change = null
    }

    val incoming = if (darkTheme) TaffyColorSchemes.dark else TaffyColorSchemes.light
    val outgoing = if (darkTheme) TaffyColorSchemes.light else TaffyColorSchemes.dark
    val blend = taffySmootherStep(taffySegment(progress, PaletteFrom, 1f))
    val colors = if (change == null) incoming else outgoing.blendTo(incoming, blend)

    // Resolved here rather than inside the branch below. `rememberVectorPainter`
    // opens a subcomposition, and creating one on the frame the transition
    // starts is the one frame in the whole run that must not hitch.
    val sun = glyphs?.let { rememberVectorPainter(it.sun) }
    val moon = glyphs?.let { rememberVectorPainter(it.moon) }

    Box(modifier = Modifier.fillMaxSize()) {
        content(colors)
        change?.let { rising ->
            ThemeSky(
                change = rising,
                progress = progress,
                colors = colors,
                painter = if (rising.toDark) moon else sun,
                modifier = Modifier
                    .fillMaxSize()
                    // A tap during the change would land on a control that is
                    // half of one theme and half of another. The window eats
                    // input for the duration rather than guessing which.
                    .pointerInput(rising) {
                        awaitPointerEventScope {
                            while (true) {
                                awaitPointerEvent().changes.forEach { it.consume() }
                            }
                        }
                    }
                    .clearAndSetSemantics { },
            )
        }
    }
}

/** The one body, climbing once, on the four phases the file header sets out. */
@Composable
private fun ThemeSky(
    change: ThemeChange,
    progress: Float,
    colors: TaffyColors,
    painter: VectorPainter?,
    modifier: Modifier = Modifier,
) {
    Canvas(modifier = modifier) {
        val rise = taffyEaseOut(taffySegment(progress, 0f, RiseUntil))
        val centre = Offset(
            x = size.width * 0.5f,
            y = size.height * (RiseFrom - (RiseFrom - RiseTo) * rise),
        )
        val radius = size.minDimension * BodyRadius * (ScaleFrom + (1f - ScaleFrom) * rise)
        // In over the opening fifth, out over the closing quarter, full in
        // between — so the body is present for the part of the movement a
        // person is actually watching, and leaves more slowly than it arrives.
        val alpha = (progress / FadeIn).coerceAtMost(1f) *
            ((1f - progress) / FadeOut).coerceAtMost(1f)
        val ink = if (change.toDark) colors.textPrimary else colors.accent

        // A soft wash behind the body, in the token that means Taffy's own
        // light. `colors` is already the blended set, so the glow travels with
        // the palette rather than sitting on top of it as a fixed colour.
        drawCircle(
            brush = Brush.radialGradient(
                colors = listOf(
                    colors.accentWash.copy(alpha = alpha * GlowAlpha),
                    Color.Transparent,
                ),
                center = centre,
                radius = radius * GlowRadius,
            ),
            radius = radius * GlowRadius,
            center = centre,
        )

        if (painter == null) {
            // No glyphs were handed in, so the drawn shapes stand in. They are
            // a fallback and not the design: see [TaffyThemeGlyphs].
            if (change.toDark) {
                drawMoon(centre, radius, ink.copy(alpha = alpha))
            } else {
                drawSun(centre, radius, ink.copy(alpha = alpha))
            }
            return@Canvas
        }
        // The vendored glyph, at the body's own size. Its 24 dp intrinsic size
        // is irrelevant here — `draw` scales it to whatever it is given.
        val extent = radius * GlyphExtent
        translate(left = centre.x - extent / 2f, top = centre.y - extent / 2f) {
            with(painter) {
                draw(size = Size(extent, extent), alpha = alpha, colorFilter = tint(ink))
            }
        }
    }
}

/**
 * A disc with eight short rays, all of which finish inside the body's own box.
 *
 * The fallback for a caller that handed in no [TaffyThemeGlyphs]; the product
 * and the previews both hand in the vendored `sun` fill glyph instead.
 */
private fun androidx.compose.ui.graphics.drawscope.DrawScope.drawSun(
    centre: Offset,
    radius: Float,
    color: Color,
) {
    val stroke = (radius * 0.09f).coerceAtLeast(2f)
    repeat(8) { index ->
        val angle = 2f * PI.toFloat() * index / 8f
        val direction = Offset(cos(angle), sin(angle))
        drawLine(
            color = color,
            start = centre + direction * radius * 1.35f,
            end = centre + direction * radius * 1.70f,
            strokeWidth = stroke,
            cap = StrokeCap.Round,
        )
    }
    drawCircle(color = color, radius = radius, center = centre)
}

/** The same fallback for the other direction; see [drawSun]. */
private fun androidx.compose.ui.graphics.drawscope.DrawScope.drawMoon(
    centre: Offset,
    radius: Float,
    color: Color,
) {
    val crescent = Path().apply {
        fillType = PathFillType.EvenOdd
        addOval(Rect(centre, radius))
        addOval(Rect(centre + Offset(radius * 0.46f, -radius * 0.26f), radius * 0.88f))
    }
    drawPath(crescent, color)
    drawCircle(color, radius * 0.14f, centre + Offset(radius * 1.55f, -radius * 0.95f))
    drawCircle(color, radius * 0.09f, centre + Offset(radius * 1.05f, -radius * 1.55f))
}

private data class ThemeChange(val toDark: Boolean)

private const val THEME_TRANSITION_MILLIS = 760

/** Where the body starts and stops, as a fraction of window height. */
private const val RiseFrom = 1.12f
private const val RiseTo = 0.30f
private const val BodyRadius = 0.09f

/**
 * Where each phase sits in the run. See the four-phase table in the file
 * header; these are the only numbers in it, and none is free — each one is the
 * boundary of a phase rather than a duration of its own.
 */
private const val PaletteFrom = 0.10f
private const val RiseUntil = 0.86f
private const val ScaleFrom = 0.86f
private const val FadeIn = 0.22f
private const val FadeOut = 0.28f

/** The wash behind the body: wide, and never more than a suggestion. */
private const val GlowRadius = 2.6f
private const val GlowAlpha = 0.55f

/** A Phosphor glyph fills its 256 viewport, so it draws wider than the disc. */
private const val GlyphExtent = 2.4f

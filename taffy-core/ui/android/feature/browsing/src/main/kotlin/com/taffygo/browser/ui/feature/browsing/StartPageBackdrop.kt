// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.drawBehind
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import com.taffygo.browser.ui.core.designsystem.TaffyTheme

/**
 * The start page's ground: a full-bleed wash the content sits on.
 *
 * Painted from theme tokens rather than an image, for the reason the old
 * wallpaper band gave: the mock's slot was a placeholder and no wallpaper
 * asset, store or setting exists, so a photograph would have invented one.
 *
 * It is **opaque** — the theme surface first, weather over it — because one of
 * its callers is the cover over the web engine's blank document, and a ground
 * the engine's white showed through would be a different colour on SCR-101
 * than on SCR-102. Over the surface sit three washes of the one accent, all
 * fading to nothing rather than meeting another colour, so there is no stop
 * where the eye can find a seam: a vertical wash that is strongest at the top
 * edge and gone above the centre, a soft glow falling from the top, and a
 * fainter answer rising from the bottom so the lower half is warmed glass
 * rather than blank paper. Light and dark each get their own weather without
 * a hand-picked colour anywhere.
 *
 * It is decoration and says so: no semantics, no size of its own, and no test
 * tag — the caller that means "this is the start page's ground" tags it, and
 * the caller that means "this covers the engine" does not, so a test never
 * finds two.
 */
@Composable
internal fun StartPageBackdrop(modifier: Modifier = Modifier) {
    val surface = TaffyTheme.colors.surface
    val accent = TaffyTheme.colors.accent
    Box(
        modifier = modifier
            .background(surface)
            .drawBehind {
                // The wash: one ramp from the top edge to nothing, above the
                // centre where the address box sits on quiet ground.
                drawRect(
                    brush = Brush.verticalGradient(
                        0f to accent.copy(alpha = WashTopAlpha),
                        WashMidStop to accent.copy(alpha = WashMidAlpha),
                        WashEndStop to Color.Transparent,
                        1f to Color.Transparent,
                    ),
                )
                val reach = maxOf(size.width, size.height)
                // The glow falling from the top: weather, not a spotlight.
                drawRect(
                    brush = Brush.radialGradient(
                        colors = listOf(accent.copy(alpha = GlowAlpha), Color.Transparent),
                        center = Offset(size.width / 2f, 0f),
                        radius = reach * GlowReach,
                    ),
                )
                // Its fainter answer from the bottom, so the colour reaches
                // the glass at both ends instead of stopping mid-page.
                drawRect(
                    brush = Brush.radialGradient(
                        colors = listOf(accent.copy(alpha = FootGlowAlpha), Color.Transparent),
                        center = Offset(size.width / 2f, size.height),
                        radius = reach * FootGlowReach,
                    ),
                )
            },
    )
}

/** The backdrop, which the start page's semantics tests name. */
const val START_BACKDROP_TEST_TAG: String = "start_backdrop"

// The wash is one continuous falloff — strongest at the glass, half gone a
// third of the way down, nothing by two thirds — so no stop ever reads as an
// edge. The glows are deliberately faint, the bottom one fainter still.
private const val WashTopAlpha = 0.20f
private const val WashMidAlpha = 0.09f
private const val WashMidStop = 0.34f
private const val WashEndStop = 0.68f
private const val GlowAlpha = 0.12f
private const val GlowReach = 0.85f
private const val FootGlowAlpha = 0.07f
private const val FootGlowReach = 0.6f

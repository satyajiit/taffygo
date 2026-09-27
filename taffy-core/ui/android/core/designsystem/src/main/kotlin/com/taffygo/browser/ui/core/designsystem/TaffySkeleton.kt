// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.designsystem

import androidx.compose.foundation.layout.Box
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.drawBehind
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Shape
import androidx.compose.ui.semantics.clearAndSetSemantics
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics

/**
 * The shape of something that has not arrived yet.
 *
 * A loading state is a promise about what is coming, so a skeleton is drawn in
 * the size and shape of the thing it stands for rather than as a spinner in the
 * middle of an empty screen. Callers give it the bounds; it fills them.
 *
 * ## Both themes
 *
 * The fill is `imagePlaceholder`, which each theme already defines for exactly
 * this — a shape with no content in it. The highlight is the next step toward
 * the surface in each direction: `surfaceRaised` on paper, `outline` on warm
 * black, because a highlight brighter than the page would be a flash rather
 * than a sheen. Neither ever carries text, so no contrast floor applies.
 *
 * ## Motion
 *
 * The sweep is the one loop this component runs, and it stops entirely when
 * [TaffyTheme.reducedMotion] is set — the accessibility obligation the UX spec
 * places on every surface. Held still it is a plain filled shape, which still
 * says "not yet" without moving in the corner of the eye. The handoff's motion
 * rule is the same one: nothing loops in peripheral vision unless something is
 * genuinely happening, and something genuinely is.
 *
 * ## Accessibility
 *
 * A skeleton is not content. With no [accessibleDescription] it clears its
 * semantics and is invisible to a screen reader, which is right when a heading
 * beside it already says the surface is loading. Pass a description on the
 * first skeleton of a surface — a localized "Loading" — so the screen is never
 * silent about why it is empty. The string comes from the caller because this
 * module has no string catalogue of its own.
 */
@Composable
fun TaffySkeleton(
    modifier: Modifier = Modifier,
    shape: Shape = TaffyTheme.shapes.card,
    accessibleDescription: String? = null,
) {
    val base = TaffyTheme.colors.imagePlaceholder
    val highlight = if (TaffyTheme.isDark) {
        TaffyTheme.colors.outline
    } else {
        TaffyTheme.colors.surfaceRaised
    }
    val still = TaffyTheme.reducedMotion

    // Keep the phase read inside drawBehind. Reading it during composition
    // would recompose the complete loading subtree on every display frame;
    // the only thing that changes is this brush, so a draw invalidation is the
    // narrow and test-idle form of the same animation.
    val progress = taffyPhase(running = !still, periodMillis = SweepDurationMillis)

    val semantics = if (accessibleDescription == null) {
        Modifier.clearAndSetSemantics { }
    } else {
        Modifier.semantics { contentDescription = accessibleDescription }
    }

    Box(
        modifier = modifier
            .then(semantics)
            .clip(shape)
            .drawBehind {
                drawRect(base)
                if (still) return@drawBehind
                val band = size.width * BandFraction
                val travel = size.width + band + band
                val x = progress.value * travel - band
                drawRect(
                    brush = Brush.linearGradient(
                        colors = listOf(Color.Transparent, highlight, Color.Transparent),
                        start = Offset(x, 0f),
                        end = Offset(x + band, 0f),
                    ),
                )
            },
    )
}

/** How wide the moving highlight is, as a fraction of the skeleton. */
private const val BandFraction = 0.4f

/** One sweep, slow enough to read as a surface rather than a strobe. */
private const val SweepDurationMillis = 1_400

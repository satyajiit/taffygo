// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxScope
import androidx.compose.foundation.layout.size
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import com.taffygo.browser.ui.core.designsystem.TaffyTheme

/** Compact 40 dp well used for grouped-card inset math. */
val TaffyGlyphFrameSize: Dp = 40.dp

/** The empty-state and identity well. */
val TaffyGlyphFrameLargeSize: Dp = 64.dp

/** Icon hosts that sit on the tile radius: 44 circle or 52–58 tile. */
val TaffyGlyphFrameHostSize: Dp = 44.dp

/**
 * A sunken orb that holds a glyph.
 *
 * Under 44 dp uses [TaffyTheme.shapes.chip]; 44–63 dp uses
 * [TaffyTheme.shapes.tile]; 64 dp and up uses [TaffyTheme.shapes.card]. Fill
 * is `surfaceSunken` unless [color] is set.
 */
@Composable
fun TaffyGlyphFrame(
    modifier: Modifier = Modifier,
    size: Dp = TaffyGlyphFrameHostSize,
    color: Color = TaffyTheme.colors.surfaceSunken,
    content: @Composable BoxScope.() -> Unit,
) {
    val shape = when {
        size >= TaffyGlyphFrameLargeSize -> TaffyTheme.shapes.card
        size >= TaffyGlyphFrameHostSize -> TaffyTheme.shapes.tile
        else -> TaffyTheme.shapes.chip
    }
    Box(
        modifier = modifier
            .size(size)
            .clip(shape)
            .background(color),
        contentAlignment = Alignment.Center,
        content = content,
    )
}

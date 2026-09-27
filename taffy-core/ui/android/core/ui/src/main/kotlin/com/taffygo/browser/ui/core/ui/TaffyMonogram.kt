// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.TextStyle
import com.taffygo.browser.ui.core.designsystem.TaffyTheme

/**
 * A person's initials over the brand sweep.
 *
 * What a profile wears before it chooses a picture, and a real choice
 * afterwards rather than a fallback — see `LocalAvatar.Monogram`. The sweep
 * is the one sampled from the 3D mark in `brand/`, the same four hues the
 * start page's site tiles use, so a face with no picture still belongs to
 * this product rather than looking like an image that failed to load.
 *
 * It draws whatever it is handed. Deciding *which* letters is
 * `LocalProfile.monogram`, in `:core:model`, so a name typed on first run and
 * the same name read back in settings cannot produce two different faces.
 */
@Composable
fun TaffyMonogram(
    monogram: String,
    modifier: Modifier = Modifier,
    letterStyle: TextStyle = TaffyTheme.typography.title,
) {
    Box(
        modifier = modifier.background(MarkRibbon),
        contentAlignment = Alignment.Center,
    ) {
        Text(
            text = monogram,
            style = letterStyle,
            color = MarkRibbonOn,
        )
    }
}

private val MarkRibbon = Brush.sweepGradient(
    colors = listOf(
        Color(0xFF2F6BFF),
        Color(0xFF9B4DFF),
        Color(0xFFFF4B9A),
        Color(0xFFFF8A52),
        Color(0xFF2F6BFF),
    ),
)
private val MarkRibbonOn = Color.White

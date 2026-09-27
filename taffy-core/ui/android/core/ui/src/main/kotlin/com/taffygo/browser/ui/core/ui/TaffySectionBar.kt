// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.width
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.taffygo.browser.ui.core.designsystem.TaffyTheme

/**
 * Section heading: an 18×4 hue bar, an 11/800 uppercase eyebrow, and a
 * heavy title.
 *
 * [count] is drawn beside the title rather than formatted into it, so a screen
 * with a list under a heading does not need a second string per language for
 * every count it might show. [countDescription] is what a screen reader hears
 * in its place — a bare number read after a heading is not a sentence. Both
 * default to null, so a heading with nothing to count is written exactly as it
 * was.
 */
@Composable
fun TaffySectionBar(
    title: String,
    modifier: Modifier = Modifier,
    eyebrow: String? = null,
    count: String? = null,
    countDescription: String? = null,
    wash: TaffyTileWash = TaffyTileWash.Accent,
) {
    Column(
        modifier = modifier.fillMaxWidth(),
        verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.tight),
    ) {
        Row(
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.tight),
        ) {
            Box(
                modifier = Modifier
                    .width(BarWidth)
                    .height(BarHeight)
                    .clip(TaffyTheme.shapes.chip)
                    .background(wash.inkColor()),
            )
            if (eyebrow != null) {
                Text(
                    text = eyebrow.uppercase(),
                    style = TaffyTheme.typography.micro.copy(
                        fontWeight = FontWeight.W800,
                    ),
                    color = wash.inkColor(),
                )
            }
        }
        Row(
            verticalAlignment = Alignment.Bottom,
            horizontalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.tight),
        ) {
            Text(
                text = title,
                style = TaffyTheme.typography.headline,
                color = TaffyTheme.colors.textPrimary,
                modifier = Modifier.semantics { heading() },
            )
            if (count != null) {
                Text(
                    text = count,
                    style = TaffyTheme.typography.headline,
                    color = TaffyTheme.colors.textSecondary,
                    modifier = Modifier.semantics {
                        contentDescription = countDescription ?: count
                    },
                )
            }
        }
    }
}

private val BarWidth = 18.dp
private val BarHeight = 4.dp

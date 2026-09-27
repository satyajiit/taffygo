// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.material3.Icon
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import com.taffygo.browser.ui.core.designsystem.TaffyBorders
import com.taffygo.browser.ui.core.designsystem.TaffyTheme

/**
 * One labelled stat on a bento grid.
 *
 * [value] is already formatted — this tile never invents a number. Radius 14,
 * a 26 dp glyph host in [wash]'s well, a heavy value on one line, and an
 * 11 sp muted label that may wrap to two. [accessibleDescription] merges the
 * pair into one announcement when the label and the value read wrongly apart.
 */
@Composable
fun TaffyStatTile(
    value: String,
    label: String,
    modifier: Modifier = Modifier,
    icon: ImageVector? = null,
    wash: TaffyTileWash = TaffyTileWash.Neutral,
    accessibleDescription: String? = null,
    testTag: String? = null,
) {
    val shape = TaffyTheme.shapes.tile
    Column(
        modifier = modifier
            .fillMaxWidth()
            .clip(shape)
            .background(TaffyTheme.colors.surfaceRaised)
            .border(TaffyBorders.standard, TaffyTheme.colors.outline, shape)
            .padding(horizontal = TaffyTheme.spacing.snug, vertical = StatVerticalPad)
            .then(if (testTag != null) Modifier.testTag(testTag) else Modifier)
            .then(
                if (accessibleDescription != null) {
                    Modifier.semantics(mergeDescendants = true) {
                        contentDescription = accessibleDescription
                    }
                } else {
                    Modifier
                },
            ),
        verticalArrangement = Arrangement.spacedBy(StatGap),
    ) {
        if (icon != null) {
            Box(
                modifier = Modifier
                    .size(GlyphHost)
                    .clip(TaffyTheme.shapes.chip)
                    .background(wash.wellColor()),
                contentAlignment = Alignment.Center,
            ) {
                Icon(
                    imageVector = icon,
                    contentDescription = null,
                    tint = wash.wellInk(),
                    modifier = Modifier.size(GlyphSize),
                )
            }
        }
        Text(
            text = value,
            style = TaffyTheme.typography.title.copy(
                fontFeatureSettings = TaffyTheme.typography.numeric.fontFeatureSettings,
            ),
            color = TaffyTheme.colors.textPrimary,
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
        )
        Text(
            text = label,
            style = TaffyTheme.typography.micro,
            color = TaffyTheme.colors.textSecondary,
            maxLines = 2,
            overflow = TextOverflow.Ellipsis,
        )
    }
}

private val GlyphHost = 26.dp
private val GlyphSize = 13.dp
private val StatVerticalPad = 11.dp
private val StatGap = 7.dp

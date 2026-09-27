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
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.material3.Icon
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import com.taffygo.browser.ui.core.designsystem.TaffyBorders
import com.taffygo.browser.ui.core.designsystem.TaffyTheme

/** Default destination-tile glyph host. */
val TaffyCategoryHostSize: Dp = 52.dp

/** You / profile chapter art tile. */
val TaffyCategoryChapterHostSize: Dp = 76.dp

/** You / profile chapter card minimum height. */
val TaffyCategoryChapterMinHeight: Dp = 106.dp

/**
 * One full-width destination tile: wash lives in the icon host, not the frame.
 *
 * [index] is already formatted ("01"). [summary] is already a string; this
 * tile never invents a count. [chapter] uses the 76 dp host and 106 dp
 * minimum. Place tiles in a column — never two-up in a row, which clips
 * titles.
 */
@Composable
fun TaffyCategoryTile(
    title: String,
    accessibleDescription: String,
    icon: ImageVector,
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
    summary: String? = null,
    index: String? = null,
    wash: TaffyTileWash = TaffyTileWash.Neutral,
    chapter: Boolean = false,
    testTag: String? = null,
) {
    val shape = TaffyTheme.shapes.card
    val host = if (chapter) TaffyCategoryChapterHostSize else TaffyCategoryHostSize
    val minHeight = if (chapter) {
        TaffyCategoryChapterMinHeight
    } else {
        TaffyTheme.spacing.minimumTouchTarget
    }
    TaffyPressable(
        onClick = onClick,
        modifier = modifier.fillMaxWidth(),
        testTag = testTag,
    ) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .heightIn(min = minHeight)
                .clip(shape)
                .background(TaffyTheme.colors.surfaceRaised)
                .border(TaffyBorders.standard, TaffyTheme.colors.outline, shape)
                .padding(TaffyTheme.spacing.cardPadding)
                .semantics(mergeDescendants = true) {
                    contentDescription = accessibleDescription
                },
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.snug),
        ) {
            Box(
                modifier = Modifier
                    .size(host)
                    .clip(TaffyTheme.shapes.tile)
                    .background(wash.washColor()),
                contentAlignment = Alignment.Center,
            ) {
                Icon(
                    imageVector = icon,
                    contentDescription = null,
                    tint = wash.inkColor(),
                    modifier = Modifier.size(HostGlyphSize),
                )
            }
            Column(
                modifier = Modifier.weight(1f),
                verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.step),
            ) {
                if (index != null) {
                    Text(
                        text = index,
                        style = TaffyTheme.typography.micro,
                        color = wash.inkColor(),
                    )
                }
                Text(
                    text = title,
                    style = TaffyTheme.typography.title,
                    color = TaffyTheme.colors.textPrimary,
                    maxLines = 2,
                    overflow = TextOverflow.Ellipsis,
                )
                if (summary != null) {
                    Text(
                        text = summary,
                        style = TaffyTheme.typography.detail,
                        color = TaffyTheme.colors.textSecondary,
                        maxLines = 2,
                        overflow = TextOverflow.Ellipsis,
                    )
                }
            }
            Icon(
                imageVector = TaffyIcon.CaretRight,
                contentDescription = null,
                tint = TaffyTheme.colors.hairline,
                modifier = Modifier.size(ChevronSize),
            )
        }
    }
}

/**
 * A two-digit chapter marker, already a string so Hindi is not asked to
 * upper-case anything. Values outside 1–99 are the caller's mistake and
 * stay visible rather than being coerced.
 */
fun taffyChapterIndex(index: Int): String = index.toString().padStart(2, '0')

private val HostGlyphSize = 24.dp
private val ChevronSize = 18.dp

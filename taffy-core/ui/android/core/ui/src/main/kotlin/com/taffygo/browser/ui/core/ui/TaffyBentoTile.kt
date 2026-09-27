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
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.material3.Icon
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.selected
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import com.taffygo.browser.ui.core.designsystem.TaffyBorders
import com.taffygo.browser.ui.core.designsystem.TaffyTheme

/** The shortest a bento tile draws, so a two-line title never crowds the well. */
val TaffyBentoTileMinHeight: Dp = 128.dp

/**
 * One destination or fact on a bento grid: a glyph well, a title that wraps
 * to two lines, an optional summary, and an optional formatted value.
 *
 * Colour follows decision 0103: the ground is [wash]'s fill over
 * `surfaceRaised`, the well is its solid, and the words stay in the ordinary
 * ink. Radius 14, a hairline border, a flat fill and no chevron. A tile with
 * an [onClick] is one pressable target that announces
 * [accessibleDescription] once; without one it is an inert tile — a premium
 * feature, a fact. [enabled] only dims the ink: the click stays, so a tile that
 * is refused in a private tab can still say so when tapped. [selected] draws
 * the emphasis border the tablet's list pane uses for the open destination.
 * [leading] replaces the glyph well with something of the caller's — a
 * provider mark — and [footer] sits under the copy. The
 * summary wraps to [summaryMaxLines], two unless a tile carries a sentence
 * rather than a phrase; tiles in a row share their height either way.
 */
@Composable
fun TaffyBentoTile(
    title: String,
    accessibleDescription: String,
    modifier: Modifier = Modifier,
    icon: ImageVector? = null,
    onClick: (() -> Unit)? = null,
    summary: String? = null,
    summaryMaxLines: Int = 2,
    value: String? = null,
    wash: TaffyTileWash = TaffyTileWash.Neutral,
    enabled: Boolean = true,
    selected: Boolean = false,
    testTag: String? = null,
    leading: (@Composable () -> Unit)? = null,
    footer: (@Composable () -> Unit)? = null,
) {
    val shape = TaffyTheme.shapes.tile
    val frame = Modifier
        .fillMaxWidth()
        .heightIn(min = TaffyBentoTileMinHeight)
        .clip(shape)
        .background(TaffyTheme.colors.surfaceRaised)
        .background(wash.fillColor())
        .border(
            width = if (selected) TaffyBorders.emphasis else TaffyBorders.standard,
            color = if (selected) TaffyTheme.colors.hairline else TaffyTheme.colors.outline,
            shape = shape,
        )
        .padding(TaffyTheme.spacing.cardPadding)
    // On the outermost node, beside the tap and the test tag, and nowhere
    // inside: a merging node is never merged into its parent, so a
    // description on the column under a pressable leaves the tapped node
    // with no name and the named node with no tap. The device suite found
    // exactly that on the workspace and provider tiles.
    val describe = Modifier.semantics(mergeDescendants = true) {
        contentDescription = accessibleDescription
        if (selected) this.selected = true
    }
    val body: @Composable () -> Unit = {
        Column(
            modifier = frame,
            verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.tight),
        ) {
            if (leading != null) {
                leading()
            } else if (icon != null) {
                TaffyGlyphFrame(color = wash.wellColor()) {
                    Icon(
                        imageVector = icon,
                        contentDescription = null,
                        tint = wash.wellInk(),
                        modifier = Modifier.size(WellGlyphSize),
                    )
                }
            }
            Text(
                text = title,
                style = TaffyTheme.typography.title,
                color = if (enabled) TaffyTheme.colors.textPrimary else TaffyTheme.colors.textTertiary,
                maxLines = 2,
                overflow = TextOverflow.Ellipsis,
            )
            if (summary != null) {
                Text(
                    text = summary,
                    style = TaffyTheme.typography.detail,
                    color = if (enabled) TaffyTheme.colors.textSecondary else TaffyTheme.colors.textTertiary,
                    maxLines = summaryMaxLines,
                    overflow = TextOverflow.Ellipsis,
                )
            }
            if (value != null) {
                Text(
                    text = value,
                    style = TaffyTheme.typography.title.copy(
                        fontFeatureSettings = TaffyTheme.typography.numeric.fontFeatureSettings,
                    ),
                    color = if (enabled) TaffyTheme.colors.textPrimary else TaffyTheme.colors.textTertiary,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                )
            }
            footer?.invoke()
        }
    }
    if (onClick != null) {
        TaffyPressable(
            onClick = onClick,
            modifier = modifier.fillMaxWidth().then(describe),
            testTag = testTag,
            content = body,
        )
    } else {
        Box(
            modifier = modifier
                .fillMaxWidth()
                .then(describe)
                .then(if (testTag != null) Modifier.testTag(testTag) else Modifier),
        ) {
            body()
        }
    }
}

private val WellGlyphSize = 22.dp

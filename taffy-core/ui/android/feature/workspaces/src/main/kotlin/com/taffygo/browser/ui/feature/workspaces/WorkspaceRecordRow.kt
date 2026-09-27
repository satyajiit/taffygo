// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.workspaces

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.material3.Icon
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.role
import androidx.compose.ui.semantics.selected
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.unit.dp
import com.taffygo.browser.ui.core.designsystem.TaffyBorders
import com.taffygo.browser.ui.core.designsystem.TaffySkeleton
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.ui.TaffyGlyphFrame
import com.taffygo.browser.ui.core.ui.TaffyGlyphFrameSize
import com.taffygo.browser.ui.core.ui.TaffyGroupedCard
import com.taffygo.browser.ui.core.ui.TaffyGroupedCardDivider
import com.taffygo.browser.ui.core.ui.TaffyIcon

/**
 * One record inside a [TaffyGroupedCard]: sunken 40 dp glyph, no own border.
 *
 * [glyphTaffyInk] and [titleTaffyInk] are the only amber on a workspace row —
 * Taffy working, or a value a conflict has touched. Selected tablet wash is
 * `surfaceSunken`, never `accentWash`.
 */
@Composable
internal fun WorkspaceRecordRow(
    title: String,
    accessibleDescription: String,
    glyph: ImageVector,
    modifier: Modifier = Modifier,
    supporting: String? = null,
    testTag: String? = null,
    selected: Boolean = false,
    enabled: Boolean = true,
    glyphTaffyInk: Boolean = false,
    titleTaffyInk: Boolean = false,
    onClick: (() -> Unit)? = null,
    trailing: @Composable (() -> Unit)? = null,
) {
    val glyphTint =
        if (glyphTaffyInk) workspaceAccentInk() else TaffyTheme.colors.textSecondary
    val titleColor =
        if (titleTaffyInk) workspaceAccentInk() else TaffyTheme.colors.textPrimary
    Row(
        modifier = modifier
            .fillMaxWidth()
            .background(if (selected) TaffyTheme.colors.surfaceSunken else Color.Transparent)
            .then(
                if (onClick != null) {
                    Modifier.clickable(enabled = enabled, onClick = onClick)
                } else {
                    Modifier
                },
            )
            .heightIn(min = RecordRowMinHeight)
            .padding(
                horizontal = TaffyTheme.spacing.screenMargin,
                vertical = TaffyTheme.spacing.snug,
            )
            .then(if (testTag != null) Modifier.testTag(testTag) else Modifier)
            .semantics(mergeDescendants = true) {
                contentDescription = accessibleDescription
                this.selected = selected
                if (onClick != null) role = Role.Button
            },
        horizontalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.snug),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        TaffyGlyphFrame {
            Icon(
                imageVector = glyph,
                contentDescription = null,
                tint = glyphTint,
                modifier = Modifier.size(GlyphSize),
            )
        }
        Column(
            modifier = Modifier.weight(1f),
            verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.step),
        ) {
            Text(
                text = title,
                style = TaffyTheme.typography.body,
                color = titleColor,
            )
            if (supporting != null) {
                Text(
                    text = supporting,
                    style = TaffyTheme.typography.detail,
                    color = TaffyTheme.colors.textSecondary,
                )
            }
        }
        trailing?.invoke()
    }
}

/** Squares Four in the 64 dp empty-state well. */
@Composable
internal fun WorkspaceEmptyGlyph(glyph: ImageVector = TaffyIcon.SquaresFour) {
    Icon(
        imageVector = glyph,
        contentDescription = null,
        tint = TaffyTheme.colors.textSecondary,
        modifier = Modifier.size(EmptyGlyphSize),
    )
}

/**
 * Four record-shaped skeletons in one grouped card. Only the first carries
 * [loadingDescription]; the rest are silent.
 */
@Composable
internal fun WorkspaceSkeletonList(
    loadingDescription: String,
    modifier: Modifier = Modifier,
    testTag: String? = null,
    count: Int = SkeletonRows,
) {
    TaffyGroupedCard(modifier = modifier, testTag = testTag) {
        repeat(count) { index ->
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .heightIn(min = RecordRowMinHeight)
                    .padding(
                        horizontal = TaffyTheme.spacing.screenMargin,
                        vertical = TaffyTheme.spacing.snug,
                    ),
                horizontalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.snug),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                TaffySkeleton(
                    modifier = Modifier.size(TaffyGlyphFrameSize),
                    shape = TaffyTheme.shapes.chip,
                    accessibleDescription = if (index == 0) loadingDescription else null,
                )
                Column(
                    modifier = Modifier.weight(1f),
                    verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.tight),
                ) {
                    TaffySkeleton(
                        modifier = Modifier
                            .fillMaxWidth(TitleFraction)
                            .height(TitleBarHeight),
                        shape = TaffyTheme.shapes.row,
                    )
                    TaffySkeleton(
                        modifier = Modifier
                            .fillMaxWidth(CaptionFraction)
                            .height(CaptionBarHeight),
                        shape = TaffyTheme.shapes.row,
                    )
                }
            }
            if (index < count - 1) TaffyGroupedCardDivider()
        }
    }
}

/** A full-width hairline for grouped cards that have no glyph well. */
@Composable
internal fun WorkspaceCardHairline(modifier: Modifier = Modifier) {
    Box(
        modifier = modifier
            .padding(horizontal = TaffyTheme.spacing.screenMargin)
            .fillMaxWidth()
            .height(TaffyBorders.standard)
            .background(TaffyTheme.colors.outline),
    )
}

@Composable
internal fun workspaceAccentInk(): Color =
    if (TaffyTheme.isDark) TaffyTheme.colors.accentText else TaffyTheme.colors.accentDeep

private val RecordRowMinHeight = 56.dp
private val GlyphSize = 20.dp
private val EmptyGlyphSize = 28.dp
private val TitleBarHeight = 12.dp
private val CaptionBarHeight = 8.dp
private const val TitleFraction = 0.6f
private const val CaptionFraction = 0.4f
private const val SkeletonRows = 4

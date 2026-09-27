// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.workspaces

import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
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
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.selected
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.unit.dp
import com.taffygo.browser.ui.core.designsystem.TaffySkeleton
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.ui.TaffyGlyphFrame
import com.taffygo.browser.ui.core.ui.TaffyGroupedCard
import com.taffygo.browser.ui.core.ui.TaffyGroupedCardDivider
import com.taffygo.browser.ui.core.ui.TaffyIcon
import com.taffygo.browser.ui.core.ui.taffyPlural

/** Four skeletons stand in for the collections that have not arrived. */
internal const val LIBRARY_SKELETON_COUNT: Int = 4

/** The 64 dp well around this is drawn by [com.taffygo.browser.ui.core.ui.TaffyEmptyState]. */
@Composable
internal fun LibraryEmptyGlyph() {
    Icon(
        imageVector = TaffyIcon.Books,
        contentDescription = null,
        tint = TaffyTheme.colors.textSecondary,
        modifier = Modifier.size(LibraryEmptyGlyphSize),
    )
}

/** A row inside a grouped Library card: glyph, title, supporting, optional mark. */
@Composable
internal fun LibraryCardRow(
    title: String,
    accessibleDescription: String,
    modifier: Modifier = Modifier,
    supporting: String? = null,
    selected: Boolean = false,
    testTag: String? = null,
    leadingIcon: ImageVector = TaffyIcon.Books,
    onClick: (() -> Unit)? = null,
    trailing: @Composable (() -> Unit)? = null,
) {
    Row(
        modifier = modifier
            .fillMaxWidth()
            .heightIn(min = LibraryRowHeight)
            .then(
                if (onClick != null) {
                    Modifier.clickable(role = Role.Button, onClick = onClick)
                } else {
                    Modifier
                },
            )
            .padding(
                horizontal = TaffyTheme.spacing.screenMargin,
                vertical = TaffyTheme.spacing.snug,
            )
            .then(if (testTag != null) Modifier.testTag(testTag) else Modifier)
            .semantics(mergeDescendants = true) {
                contentDescription = accessibleDescription
                this.selected = selected
            },
        horizontalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.snug),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        TaffyGlyphFrame {
            Icon(
                imageVector = leadingIcon,
                contentDescription = null,
                tint = TaffyTheme.colors.textSecondary,
                modifier = Modifier.size(LibraryGlyphSize),
            )
        }
        Column(
            modifier = Modifier.weight(1f),
            verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.step),
        ) {
            Text(
                text = title,
                style = TaffyTheme.typography.title,
                color = TaffyTheme.colors.textPrimary,
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

/**
 * Conflict, as a word and a warning glyph.
 *
 * Colour is not the only signal: the count is spoken and drawn.
 */
@Composable
internal fun LibraryConflictMark(conflictCount: Int, modifier: Modifier = Modifier) {
    if (conflictCount <= 0) return
    Row(
        modifier = modifier,
        horizontalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.tight),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Icon(
            imageVector = TaffyIcon.Warning,
            contentDescription = null,
            tint = TaffyTheme.colors.dangerText,
            modifier = Modifier.size(LibraryGlyphSize),
        )
        Text(
            text = taffyPlural(
                R.plurals.taffy_library_conflict_count,
                conflictCount,
                conflictCount,
            ),
            style = TaffyTheme.typography.caption,
            color = TaffyTheme.colors.dangerText,
        )
    }
}

/** Grouped card of [count] rows, with hairlines between them. */
@Composable
internal fun LibraryGroupedItems(
    count: Int,
    modifier: Modifier = Modifier,
    testTag: String? = null,
    item: @Composable (index: Int) -> Unit,
) {
    TaffyGroupedCard(modifier = modifier, testTag = testTag) {
        repeat(count) { index ->
            item(index)
            if (index != count - 1) TaffyGroupedCardDivider()
        }
    }
}

/** Skeleton rows in the shape of the list that will replace them. */
@Composable
internal fun LibrarySkeletonList(description: String, modifier: Modifier = Modifier) {
    Column(
        modifier = modifier,
        verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.tight),
    ) {
        repeat(LIBRARY_SKELETON_COUNT) { row ->
            TaffySkeleton(
                shape = TaffyTheme.shapes.card,
                accessibleDescription = if (row == 0) description else null,
                modifier = Modifier
                    .fillMaxWidth()
                    .height(LibraryRowHeight)
                    .then(
                        if (row == 0) {
                            Modifier.testTag(LIBRARY_LOADING_TEST_TAG)
                        } else {
                            Modifier
                        },
                    ),
            )
        }
    }
}

/** The tag the first skeleton carries, so a semantics test names the wait. */
const val LIBRARY_LOADING_TEST_TAG: String = "library_loading"

private val LibraryRowHeight = 72.dp
private val LibraryGlyphSize = 20.dp
private val LibraryEmptyGlyphSize = 32.dp

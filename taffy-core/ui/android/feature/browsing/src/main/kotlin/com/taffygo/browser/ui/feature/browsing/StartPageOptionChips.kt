// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.material3.Icon
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.key
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import com.taffygo.browser.ui.core.designsystem.TaffyBorders
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.ui.TaffyIcon
import com.taffygo.browser.ui.core.ui.taffyString

/**
 * The options a person has set on this question, as a line of small chips.
 *
 * A list rather than one nullable field on the row, because "what has been
 * applied to this question" is a set that will grow, and a row that holds one
 * thing by construction has to be rebuilt to hold two. Today the set has three
 * kinds of member: the pages the question is about, on the Ask overlay's box
 * ([StartPagePageChip]); the shape stated from the plus; and the stores
 * attached from it (decision 0133), each with the store's own glyph so a chip
 * reads at a glance as a thing Taffy may search rather than a thing the job is.
 */
@Composable
internal fun StartPageOptionChips(
    state: AddressBarUiState,
    onIntent: (AddressBarIntent) -> Unit,
) {
    val shape = state.shape
    val stores = state.attachedStores.sortedBy { it.ordinal }
    val pages = state.attachedPages
    if (shape == null && stores.isEmpty() && pages.isEmpty()) return
    FlowRow(
        modifier = Modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.tight),
        verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.tight),
    ) {
        pages.forEach { page ->
            key(page.tabId.value) {
                StartPagePageChip(
                    page = page,
                    onRemove = if (page.taffyOpened) {
                        null
                    } else {
                        { onIntent(AddressBarIntent.RemovePage(page.tabId)) }
                    },
                )
            }
        }
        if (shape != null) {
            val label = taffyString(templateChipLabel(shape))
            OptionChip(
                label = label,
                removeDescription = taffyString(R.string.taffy_address_bar_chip_remove, label),
                onRemove = { onIntent(AddressBarIntent.ClearShape) },
                testTag = "$START_SHAPE_CHIP_TEST_TAG_PREFIX${shape.label}",
            )
        }
        stores.forEach { store ->
            val label = taffyString(storeLabel(store))
            OptionChip(
                label = label,
                removeDescription = taffyString(R.string.taffy_address_bar_chip_remove, label),
                onRemove = { onIntent(AddressBarIntent.ToggleStore(store)) },
                testTag = "$START_STORE_CHIP_TEST_TAG_PREFIX${store.label}",
                icon = storeIcon(store),
            )
        }
    }
}

@Composable
private fun OptionChip(
    label: String,
    removeDescription: String,
    onRemove: () -> Unit,
    testTag: String,
    icon: ImageVector? = null,
) {
    val colors = TaffyTheme.colors
    val pill = TaffyTheme.shapes.pill
    Row(
        modifier = Modifier
            .height(ChipHeight)
            .clip(pill)
            .background(colors.surfaceRaised)
            .border(TaffyBorders.standard, colors.outline, pill)
            .padding(start = TaffyTheme.spacing.snug)
            .testTag(testTag),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.tight),
    ) {
        if (icon != null) {
            Icon(
                imageVector = icon,
                contentDescription = null,
                tint = colors.textSecondary,
                modifier = Modifier.size(ChipGlyphSize),
            )
        }
        Text(
            text = label,
            style = TaffyTheme.typography.detail,
            color = colors.textPrimary,
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
        )
        Box(
            modifier = Modifier
                .size(ChipHeight)
                .clickable(role = Role.Button, onClick = onRemove)
                .semantics { contentDescription = removeDescription },
            contentAlignment = Alignment.Center,
        ) {
            Icon(
                imageVector = TaffyIcon.X,
                contentDescription = null,
                tint = colors.textSecondary,
                modifier = Modifier.size(ChipGlyphSize),
            )
        }
    }
}

/** One option chip, by the shape it names. */
const val START_SHAPE_CHIP_TEST_TAG_PREFIX: String = "start_shape_"

// Small beside the box on purpose: a chip is a note on the question, not a
// second control competing with the field above it.
private val ChipHeight = 32.dp
private val ChipGlyphSize = 14.dp

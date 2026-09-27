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
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Icon
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.unit.dp
import com.taffygo.browser.ui.core.designsystem.TaffyBorders
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.ui.TaffyIcon
import com.taffygo.browser.ui.core.ui.taffyPlural
import com.taffygo.browser.ui.core.ui.taffyString

/**
 * Taffy's owned-tab header as one neutral group surface.
 *
 * Amber stays on the ownership mark, legend, and owned cards. The group shell
 * itself remains neutral, so expanding it does not turn a whole section into a
 * status colour. The cards are separate lazy-grid items; this module never
 * accepts or iterates the complete owned-tab list.
 */
@Composable
internal fun TaffyTabsGroupHeader(
    state: TabSwitcherUiState,
    onIntent: (TabSwitcherIntent) -> Unit,
) {
    val actionDescription = taffyString(
        if (state.taffyGroupExpanded) {
            R.string.taffy_tab_switcher_hide_taffy_tabs
        } else {
            R.string.taffy_tab_switcher_show_taffy_tabs
        },
        state.taffyTabs.size,
    )
    Column(
        modifier = Modifier
            .fillMaxWidth()
            .clip(TaffyTheme.shapes.card)
            .background(TaffyTheme.colors.surfaceRaised)
            .border(TaffyBorders.standard, TaffyTheme.colors.outline, TaffyTheme.shapes.card),
    ) {
        TaffyGroupHeader(
            state = state,
            actionDescription = actionDescription,
            onClick = { onIntent(TabSwitcherIntent.ToggleTaffyGroup) },
        )
        // Outside the expander, and mock 04 agrees: the amber edge is drawn on
        // cards in the grid above this group as well as on the ones inside it,
        // so a legend that appeared only when the group was opened explained a
        // treatment the person could already see and could not yet read. Parity
        // row PAR-A11Y-004 forbids a status carried by colour alone, and a
        // collapsed group left exactly that on screen.
        AmberLegend(
            modifier = Modifier.padding(
                start = TaffyTheme.spacing.snug,
                end = TaffyTheme.spacing.snug,
                bottom = TaffyTheme.spacing.snug,
            ),
        )
    }
}

/**
 * One lazy cell in Taffy's expanded group.
 *
 * The sunken well keeps the expanded group's tonal treatment around the amber
 * card without introducing a second same-axis lazy layout. One cell is the
 * largest amount of owned-tab UI this module can compose at once; the outer
 * grid decides which cells exist.
 */
@Composable
internal fun TaffyGroupTabCell(
    card: TabCard,
    isSelecting: Boolean,
    onIntent: (TabSwitcherIntent) -> Unit,
    modifier: Modifier = Modifier,
) {
    Box(
        modifier = modifier
            .fillMaxWidth()
            .clip(TaffyTheme.shapes.card)
            .background(TaffyTheme.colors.surfaceSunken)
            .padding(TaffyTheme.spacing.snug),
    ) {
        TabSwitcherCard(
            card = card,
            isSelecting = isSelecting,
            onIntent = onIntent,
            modifier = Modifier.fillMaxWidth(),
        )
    }
}

/** The group identity, count, and one honest expand-or-collapse action. */
@Composable
private fun TaffyGroupHeader(
    state: TabSwitcherUiState,
    actionDescription: String,
    onClick: () -> Unit,
) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .clickable(onClick = onClick, role = Role.Button)
            .heightIn(min = GroupHeaderMinimumHeight)
            .padding(TaffyTheme.spacing.snug)
            .testTag(TAFFY_GROUP_TEST_TAG)
            .semantics(mergeDescendants = true) {
                contentDescription = actionDescription
            },
        horizontalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.snug),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Box(
            modifier = Modifier
                .size(GroupGlyphWellSize)
                .clip(TaffyTheme.shapes.row)
                .background(TaffyTheme.colors.accentWash),
            contentAlignment = Alignment.Center,
        ) {
            Icon(
                imageVector = TaffyIcon.Sparkle,
                contentDescription = null,
                modifier = Modifier.size(GroupGlyphSize),
                tint = TaffyTheme.colors.accent,
            )
        }
        Column(modifier = Modifier.weight(1f)) {
            Text(
                text = taffyString(R.string.taffy_tab_switcher_taffy_tabs),
                style = TaffyTheme.typography.title,
                color = TaffyTheme.colors.textPrimary,
            )
            Text(
                text = taffyPlural(
                    R.plurals.taffy_tab_switcher_tab_count,
                    state.taffyTabs.size,
                    state.taffyTabs.size,
                ),
                style = TaffyTheme.typography.numeric,
                color = TaffyTheme.colors.textSecondary,
            )
        }
        Icon(
            imageVector = TaffyIcon.CaretDown,
            contentDescription = null,
            modifier = Modifier
                .size(GroupCaretSize)
                .graphicsLayer {
                    rotationZ = if (state.taffyGroupExpanded) 180f else 0f
                },
            tint = TaffyTheme.colors.textSecondary,
        )
    }
}

/**
 * What the amber edge means, said in words next to it.
 *
 * Parity row PAR-A11Y-004 forbids a status carried by colour alone, so the
 * group names the ownership treatment once instead of making it an inference.
 * It is stated once for the whole screen, under the group header, because the
 * treatment it explains appears on cards outside this group too.
 */
@Composable
private fun AmberLegend(modifier: Modifier = Modifier) {
    Row(
        modifier = modifier.fillMaxWidth().testTag(LEGEND_TEST_TAG),
        horizontalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.tight),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Box(
            modifier = Modifier
                .size(LegendSwatchSize)
                .clip(LegendSwatchShape)
                .background(TaffyTheme.colors.accent),
        )
        Text(
            text = taffyString(R.string.taffy_tab_switcher_amber_legend),
            style = TaffyTheme.typography.caption,
            color = TaffyTheme.colors.textSecondary,
        )
    }
}

private val LegendSwatchSize = 9.dp
private val LegendSwatchShape = RoundedCornerShape(3.dp)
private val GroupGlyphWellSize = 40.dp
private val GroupGlyphSize = 18.dp
private val GroupCaretSize = 17.dp
private val GroupHeaderMinimumHeight = 64.dp

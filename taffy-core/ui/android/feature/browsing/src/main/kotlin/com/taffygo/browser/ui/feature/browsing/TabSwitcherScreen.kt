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
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.material3.Icon
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.drawBehind
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.PathEffect
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import com.taffygo.browser.ui.core.designsystem.TaffyBorders
import com.taffygo.browser.ui.core.designsystem.TaffyRadii
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.ui.TaffyBottomSheet
import com.taffygo.browser.ui.core.ui.TaffyDangerButton
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyEmptyState
import com.taffygo.browser.ui.core.ui.TaffyIcon
import com.taffygo.browser.ui.core.ui.TaffyIconButton
import com.taffygo.browser.ui.core.ui.TaffyNavigator
import com.taffygo.browser.ui.core.ui.TaffyScreen
import com.taffygo.browser.ui.core.ui.TaffySecondaryButton
import com.taffygo.browser.ui.core.ui.TaffySegmentedControl
import com.taffygo.browser.ui.core.ui.screenViewModel
import com.taffygo.browser.ui.core.ui.taffyPlural
import com.taffygo.browser.ui.core.ui.taffyString

/** Screen SCR-104 — the tab switcher. */
@Composable
fun TabSwitcherScreen(
    navigator: TaffyNavigator,
    modifier: Modifier = Modifier,
) {
    val viewModel: TabSwitcherViewModel = screenViewModel(TaffyDestination.TabSwitcher)
    val state by viewModel.state.collectAsStateWithLifecycle()

    LaunchedEffect(Unit) { viewModel.onShown() }

    TabSwitcherContent(
        state = state,
        onIntent = { viewModel.onIntent(it, navigator) },
        modifier = modifier,
    )
}

/**
 * The stateless half, which is what a preview and a semantics test render.
 *
 * The design document's mock 04, reconciled with screen catalog row SCR-104
 * where the two differ. The grid, the segmented control, the amber edge, the
 * per-card fact count and the count line are the mock's; Taffy's tabs keep the
 * collapsed group of their own that the catalog row asks for, and the amber
 * edge is how a card in it is drawn wherever it appears.
 */
@Composable
fun TabSwitcherContent(
    state: TabSwitcherUiState,
    onIntent: (TabSwitcherIntent) -> Unit,
    modifier: Modifier = Modifier,
) {
    TaffyScreen(
        destination = TaffyDestination.TabSwitcher,
        title = null,
        modifier = modifier,
        scrollable = false,
        header = {
            GroupControl(state = state, onIntent = onIntent)
            TabSearchField(
                query = state.searchQuery,
                onQueryChange = { onIntent(TabSwitcherIntent.Search(it)) },
            )
        },
        footer = {
            TabSwitcherAskBar(state = state, onIntent = onIntent)
            CountRow(state = state, onIntent = onIntent)
        },
    ) {
        LazyTabSwitcherGrid(state = state, onIntent = onIntent)
    }

    if (state.closeVisibleConfirmationVisible) {
        CloseVisibleConfirmation(onIntent = onIntent)
    }
}

/**
 * Closing every tab in a segment is not undoable, so it is asked first.
 *
 * The body names the limit rather than leaving it implied: a running task's
 * tabs survive this, because the task cites the pages it read and a switcher
 * that silently closed them would break it without saying so. A tab of
 * Taffy's that no task still holds closes with the rest
 * ([TabSwitcherUiState.closeVisibleTabs]).
 */
@Composable
private fun CloseVisibleConfirmation(onIntent: (TabSwitcherIntent) -> Unit) {
    TaffyBottomSheet(
        title = taffyString(R.string.taffy_tab_switcher_close_all_title),
        onDismissRequest = { onIntent(TabSwitcherIntent.DismissCloseVisible) },
        testTag = CLOSE_ALL_SHEET_TEST_TAG,
    ) {
        Text(
            text = taffyString(R.string.taffy_tab_switcher_close_all_body),
            style = TaffyTheme.typography.body,
            color = TaffyTheme.colors.textSecondary,
        )
        TaffyDangerButton(
            label = taffyString(R.string.taffy_tab_switcher_close_all_confirm),
            onClick = { onIntent(TabSwitcherIntent.ConfirmCloseVisible) },
            modifier = Modifier.fillMaxWidth(),
            testTag = CLOSE_ALL_CONFIRM_TEST_TAG,
        )
        TaffySecondaryButton(
            label = taffyString(R.string.taffy_tab_switcher_close_all_cancel),
            onClick = { onIntent(TabSwitcherIntent.DismissCloseVisible) },
            modifier = Modifier.fillMaxWidth(),
            testTag = CLOSE_ALL_CANCEL_TEST_TAG,
        )
    }
}

/**
 * The two groups, as one control.
 *
 * It fills the width where mock 04 centres it at its content size, and the two
 * decisions are the same decision. The mock can centre the pill because its
 * search is a 44 dp glyph pinned to the right edge; screen catalog row SCR-104
 * keeps a real search field, which is a full-width control directly underneath,
 * and a content-width pill above a full-width field reads as a mistake rather
 * than as a choice. Width is also what makes the control survive parity row
 * PAR-A11Y-003: at twice the text size, "Your tabs" and its glyph do not fit a
 * segment two thirds this wide.
 */
@Composable
private fun GroupControl(state: TabSwitcherUiState, onIntent: (TabSwitcherIntent) -> Unit) {
    val options = listOf(
        taffyString(R.string.taffy_tab_switcher_your_tabs),
        taffyString(R.string.taffy_tab_switcher_private),
    )
    TaffySegmentedControl(
        options = options,
        selectedIndex = state.group.ordinal,
        onSelect = { index ->
            onIntent(TabSwitcherIntent.SelectGroup(TabSwitcherGroup.entries[index]))
        },
        modifier = Modifier.fillMaxWidth(),
        // Mock 04 puts a glyph beside each label. It is decoration only — the
        // word beside it already says what the segment selects.
        optionIcon = { index ->
            if (index == TabSwitcherGroup.PRIVATE.ordinal) {
                TaffyIcon.EyeSlash
            } else {
                TaffyIcon.Browsers
            }
        },
        optionModifier = { index ->
            Modifier.testTag(
                if (index == TabSwitcherGroup.PRIVATE.ordinal) {
                    GROUP_PRIVATE_TEST_TAG
                } else {
                    GROUP_YOURS_TEST_TAG
                },
            )
        },
    )
}

/**
 * "New workspace from six tabs…".
 *
 * It opens the Ask sheet with the source-table shape stated, and its spoken
 * label says so; the tabs it counts are the ones the sheet attaches.
 *
 * The number is [TabSwitcherUiState.workspaceTabCount] and not the total, for
 * the reason written there: this label is a promise about scope, and a private
 * tab is never in one.
 */
@Composable
internal fun StartWorkspaceAction(
    state: TabSwitcherUiState,
    onIntent: (TabSwitcherIntent) -> Unit,
) {
    val label = taffyPlural(
        R.plurals.taffy_tab_switcher_new_workspace,
        state.workspaceTabCount,
        state.workspaceTabCount,
    )
    val description = taffyString(R.string.taffy_tab_switcher_new_workspace_description, label)
    // Dashed, as mock 04 draws it, and the dash is doing work: every other
    // bordered thing on this screen is a tab that exists, and this is an offer
    // to make something that does not exist yet. A solid outline made it look
    // like a seventh card. There is no dashed variant of `Modifier.border`, so
    // the stroke is drawn rather than declared.
    val dash = TaffyTheme.colors.hairline
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .clip(TaffyTheme.shapes.row)
            .drawBehind {
                val stroke = TaffyBorders.standard.toPx()
                val radius = CornerRadius(TaffyRadii.row.toPx())
                drawRoundRect(
                    color = dash,
                    topLeft = Offset(stroke / 2f, stroke / 2f),
                    size = Size(size.width - stroke, size.height - stroke),
                    cornerRadius = radius,
                    style = Stroke(
                        width = stroke,
                        pathEffect = PathEffect.dashPathEffect(
                            floatArrayOf(DashOn.toPx(), DashOff.toPx()),
                        ),
                    ),
                )
            }
            .clickable { onIntent(TabSwitcherIntent.StartWorkspace) }
            .heightIn(min = TaffyTheme.spacing.minimumTouchTarget)
            .padding(TaffyTheme.spacing.snug)
            .testTag(NEW_WORKSPACE_TEST_TAG)
            .semantics(mergeDescendants = true) { contentDescription = description },
        horizontalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.snug),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Icon(
            // The workspace glyph, which is `Table` everywhere else in the
            // product. `Article` is a document, and a workspace is not one.
            imageVector = TaffyIcon.Table,
            contentDescription = null,
            modifier = Modifier.size(ActionGlyphSize),
            tint = TaffyTheme.colors.textSecondary,
        )
        Text(
            text = label,
            style = TaffyTheme.typography.body,
            color = TaffyTheme.colors.textSecondary,
        )
    }
}

/**
 * The fixed row under the grid: open a tab, and what is open said in words.
 *
 * The count line is the design document's, kept because it is the one place
 * "three of these are Taffy's" is stated rather than drawn.
 */
@Composable
private fun CountRow(state: TabSwitcherUiState, onIntent: (TabSwitcherIntent) -> Unit) {
    val counts = taffyString(
        R.string.taffy_tab_switcher_counts,
        taffyPlural(
            R.plurals.taffy_tab_switcher_tab_count,
            state.totalTabCount,
            state.totalTabCount,
        ),
        taffyPlural(
            R.plurals.taffy_tab_switcher_opened_by_taffy,
            state.taffyTabs.size,
            state.taffyTabs.size,
        ),
    )
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .clip(TaffyTheme.shapes.row)
            .background(TaffyTheme.colors.surfaceRaised)
            .border(TaffyBorders.standard, TaffyTheme.colors.outline, TaffyTheme.shapes.row)
            .heightIn(min = CountRowMinimumHeight)
            .padding(horizontal = TaffyTheme.spacing.tight),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Box(modifier = Modifier.weight(1f), contentAlignment = Alignment.CenterStart) {
            TaffyIconButton(
                icon = TaffyIcon.Plus,
                // The glyph is the same in both segments and what it opens is
                // not, so the spoken label is the one that has to differ: a
                // person who cannot see which segment is selected is told
                // which kind of tab this makes before they make one.
                contentDescription = taffyString(
                    if (state.group == TabSwitcherGroup.PRIVATE) {
                        R.string.taffy_tab_switcher_new_private_tab
                    } else {
                        R.string.taffy_tab_switcher_new_tab
                    },
                ),
                onClick = { onIntent(TabSwitcherIntent.NewTab) },
                testTag = NEW_TAB_TEST_TAG,
            )
        }
        Text(
            text = counts,
            style = TaffyTheme.typography.numeric,
            color = TaffyTheme.colors.textSecondary,
            textAlign = TextAlign.Center,
            modifier = Modifier.weight(3f).testTag(COUNTS_TEST_TAG),
        )
        Box(modifier = Modifier.weight(1f), contentAlignment = Alignment.CenterEnd) {
            // Mock 04's second control. Drawn only when there is something to
            // close, so the slot is never a button that does nothing — the
            // count stays optically centred either way because the box holds
            // its weight whether or not it has a child. Asked of what closing
            // would close, so a segment holding only Taffy's finished tabs
            // still offers it.
            if (state.closeVisibleTabs.isNotEmpty()) {
                TaffyIconButton(
                    icon = TaffyIcon.Trash,
                    contentDescription = taffyString(R.string.taffy_tab_switcher_close_all),
                    onClick = { onIntent(TabSwitcherIntent.RequestCloseVisible) },
                    testTag = CLOSE_ALL_TEST_TAG,
                )
            }
        }
    }
}

@Composable
internal fun EmptyGroup(group: TabSwitcherGroup) {
    val private = group == TabSwitcherGroup.PRIVATE
    TaffyEmptyState(
        title = taffyString(
            if (private) R.string.taffy_tab_switcher_private_empty_title else R.string.taffy_tab_switcher_empty_title,
        ),
        body = taffyString(
            if (private) R.string.taffy_tab_switcher_private_empty_body else R.string.taffy_tab_switcher_empty_body,
        ),
    )
}

/** The tags screen SCR-104's semantics tests name. */
const val NEW_TAB_TEST_TAG: String = "tab_switcher_new_tab"
const val TAB_GRID_TEST_TAG: String = "tab_switcher_grid"
const val TAFFY_GROUP_TEST_TAG: String = "tab_switcher_taffy_group"
const val TAB_TEST_TAG_PREFIX: String = "tab_switcher_tab_"
const val CLOSE_TEST_TAG_PREFIX: String = "tab_switcher_close_"
const val PREVIEW_TEST_TAG_PREFIX: String = "tab_switcher_preview_"
const val GROUP_YOURS_TEST_TAG: String = "tab_switcher_group_yours"
const val GROUP_PRIVATE_TEST_TAG: String = "tab_switcher_group_private"
const val LEGEND_TEST_TAG: String = "tab_switcher_legend"
const val NEW_WORKSPACE_TEST_TAG: String = "tab_switcher_new_workspace"
const val COUNTS_TEST_TAG: String = "tab_switcher_counts"
const val CLOSE_ALL_TEST_TAG: String = "tab_switcher_close_all"
const val CLOSE_ALL_SHEET_TEST_TAG: String = "tab_switcher_close_all_sheet"
const val CLOSE_ALL_CONFIRM_TEST_TAG: String = "tab_switcher_close_all_confirm"
const val CLOSE_ALL_CANCEL_TEST_TAG: String = "tab_switcher_close_all_cancel"
const val FAVICON_TEST_TAG_PREFIX: String = "tab_switcher_favicon_"
const val AGE_TEST_TAG_PREFIX: String = "tab_switcher_age_"

// The glyph beside the workspace action and the navigation surface heights.
private val ActionGlyphSize = 17.dp

private val DashOn = 5.dp
private val DashOff = 4.dp
private val CountRowMinimumHeight = 56.dp

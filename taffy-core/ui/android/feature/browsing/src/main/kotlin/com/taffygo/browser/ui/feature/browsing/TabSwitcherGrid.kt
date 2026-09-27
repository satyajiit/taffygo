// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.lazy.grid.GridCells
import androidx.compose.foundation.lazy.grid.GridItemSpan
import androidx.compose.foundation.lazy.grid.LazyGridScope
import androidx.compose.foundation.lazy.grid.LazyVerticalGrid
import androidx.compose.foundation.lazy.grid.items
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.ui.TaffyEmptyState
import com.taffygo.browser.ui.core.ui.taffyString

/**
 * The switcher's unbounded top-level collection.
 *
 * A browser profile can hold far more tabs than fit on one screen. Keeping the
 * grid lazy means opening the switcher measures only the visible cards instead
 * of decoding and composing every thumbnail in the profile. Expanded Taffy
 * tabs are ordinary keyed items in this same scroll owner: the browsing
 * contract admits 256, so an eager nested column is not a bounded exception.
 */
@Composable
internal fun LazyTabSwitcherGrid(
    state: TabSwitcherUiState,
    onIntent: (TabSwitcherIntent) -> Unit,
    modifier: Modifier = Modifier,
) {
    // Both are derived getters. Read each once so a search allocates one
    // filtered list per composition instead of one per branch and item block.
    val visibleTabs = state.visibleTabs
    val matchingTabs = state.matchingTabs
    LazyVerticalGrid(
        columns = GridCells.Fixed(GridColumns),
        modifier = modifier
            .fillMaxSize()
            .testTag(TAB_GRID_TEST_TAG),
        horizontalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.snug),
        verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.snug),
    ) {
        when {
            visibleTabs.isEmpty() -> fullWidthItem(key = "empty-group") {
                EmptyGroup(group = state.group)
            }
            state.searchFoundNothing -> fullWidthItem(key = "empty-search") {
                TaffyEmptyState(
                    title = taffyString(R.string.taffy_tab_switcher_search_empty_title),
                    body = taffyString(
                        R.string.taffy_tab_switcher_search_empty_body,
                        state.searchQuery.trim(),
                    ),
                )
            }
            else -> items(
                items = matchingTabs,
                key = { card -> "tab-${card.id.value}" },
                contentType = { RegularTabContentType },
            ) { card ->
                TabSwitcherCard(
                    card = card,
                    isSelecting = state.isSelecting,
                    onIntent = onIntent,
                    modifier = Modifier.fillMaxWidth(),
                )
            }
        }

        if (state.showsTaffyGroup) {
            fullWidthItem(key = "taffy-group") {
                TaffyTabsGroupHeader(state = state, onIntent = onIntent)
            }
            if (state.taffyGroupExpanded) {
                items(
                    items = state.taffyTabs,
                    key = { card -> "taffy-tab-${card.id.value}" },
                    contentType = { TaffyTabContentType },
                ) { card ->
                    TaffyGroupTabCell(
                        card = card,
                        isSelecting = state.isSelecting,
                        onIntent = onIntent,
                    )
                }
            }
        }

        if (state.canStartWorkspace) {
            fullWidthItem(key = "start-workspace") {
                StartWorkspaceAction(state = state, onIntent = onIntent)
            }
        }
    }
}

private fun LazyGridScope.fullWidthItem(
    key: String,
    content: @Composable () -> Unit,
) {
    item(key = key, contentType = key, span = { GridItemSpan(maxLineSpan) }) { content() }
}

/** The design document's mock 04 draws two columns on a phone. */
private const val GridColumns = 2
private const val RegularTabContentType = "regular-tab"
private const val TaffyTabContentType = "taffy-tab"

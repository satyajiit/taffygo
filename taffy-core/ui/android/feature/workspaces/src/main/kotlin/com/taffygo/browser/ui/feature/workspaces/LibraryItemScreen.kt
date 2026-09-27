// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.workspaces

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.ui.Modifier
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.ui.TaffyDangerButton
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyEmptyState
import com.taffygo.browser.ui.core.ui.TaffyIcon
import com.taffygo.browser.ui.core.ui.TaffyNavigator
import com.taffygo.browser.ui.core.ui.TaffyScreen
import com.taffygo.browser.ui.core.ui.TaffySectionHeader
import com.taffygo.browser.ui.core.ui.TaffySourceChip
import com.taffygo.browser.ui.core.ui.screenViewModel
import com.taffygo.browser.ui.core.ui.taffyString

/**
 * Screen SCR-503 — one kept item.
 *
 * Sources and related items appear only when the port named them. Remove stays
 * off while the port cannot mutate, so the screen never claims a deletion.
 */
@Composable
fun LibraryItemScreen(
    destination: TaffyDestination.LibraryItem,
    navigator: TaffyNavigator,
    modifier: Modifier = Modifier,
    showUp: Boolean = true,
) {
    val viewModel: LibraryItemViewModel = screenViewModel(destination)
    val state by viewModel.state.collectAsStateWithLifecycle()

    LaunchedEffect(Unit) { viewModel.onShown() }

    LibraryItemContent(
        state = state,
        onIntent = { viewModel.onIntent(it, navigator) },
        onBack = if (showUp) ({ navigator.goBack() }) else null,
        modifier = modifier,
    )
}

/** The stateless half. */
@Composable
fun LibraryItemContent(
    state: LibraryItemUiState,
    onIntent: (LibraryItemIntent) -> Unit,
    modifier: Modifier = Modifier,
    onBack: (() -> Unit)? = null,
) {
    // The empty-state card owns the missing sentence. A neutral route title
    // prevents the same heading from being announced twice.
    val title = state.title.ifEmpty { taffyString(R.string.taffy_library_home_title) }
    TaffyScreen(
        destination = TaffyDestination.LibraryItem(state.collectionId, state.itemId),
        title = title,
        onBack = onBack,
        modifier = modifier,
        footer = if (state.loading || state.unavailable || state.missing) {
            null
        } else {
            { LibraryItemActions(state = state, onIntent = onIntent) }
        },
    ) {
        when {
            state.loading -> LibrarySkeletonList(
                description = taffyString(R.string.taffy_library_item_loading),
            )
            state.unavailable -> TaffyEmptyState(
                title = taffyString(R.string.taffy_library_unavailable_title),
                body = taffyString(R.string.taffy_library_unavailable_body),
                leading = { LibraryEmptyGlyph() },
            )
            state.missing -> TaffyEmptyState(
                title = taffyString(R.string.taffy_library_item_missing_title),
                body = taffyString(R.string.taffy_library_item_missing_body),
                leading = { LibraryEmptyGlyph() },
            )
            else -> LibraryItemBody(state = state, onIntent = onIntent)
        }
    }
}

@Composable
private fun LibraryItemBody(
    state: LibraryItemUiState,
    onIntent: (LibraryItemIntent) -> Unit,
) {
    if (state.hasConflict) {
        LibraryCardRow(
            title = taffyString(R.string.taffy_library_item_conflict),
            supporting = state.conflictSummary,
            accessibleDescription = if (state.conflictSummary != null) {
                taffyString(
                    R.string.taffy_library_item_description_conflict,
                    taffyString(R.string.taffy_library_item_conflict),
                    state.conflictSummary,
                )
            } else {
                taffyString(
                    R.string.taffy_library_item_description,
                    taffyString(R.string.taffy_library_item_conflict),
                )
            },
            testTag = LIBRARY_ITEM_CONFLICT_TEST_TAG,
            leadingIcon = TaffyIcon.Warning,
            trailing = { LibraryConflictMark(conflictCount = 1) },
        )
    }
    if (state.body.isNotEmpty()) {
        Text(
            text = state.body,
            style = TaffyTheme.typography.body,
            color = TaffyTheme.colors.textPrimary,
            modifier = Modifier,
        )
    }
    state.capturedAt?.let { captured ->
        Text(
            text = taffyString(R.string.taffy_library_item_captured, captured),
            style = TaffyTheme.typography.detail,
            color = TaffyTheme.colors.textSecondary,
        )
    }
    state.freshness?.let { freshness ->
        Text(
            text = freshness,
            style = TaffyTheme.typography.detail,
            color = TaffyTheme.colors.textSecondary,
        )
    }
    if (state.sources.isNotEmpty()) {
        TaffySectionHeader(title = taffyString(R.string.taffy_library_item_sources))
        Column(verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.tight)) {
            state.sources.forEach { source ->
                TaffySourceChip(
                    host = source.host,
                    accessibleDescription = taffyString(
                        R.string.taffy_library_item_source_chip,
                        source.host,
                    ),
                    modifier = Modifier,
                )
            }
        }
    }
    if (state.related.isNotEmpty()) {
        TaffySectionHeader(title = taffyString(R.string.taffy_library_item_related))
        LibraryGroupedItems(count = state.related.size) { index ->
            val related = state.related[index]
            LibraryCardRow(
                title = related.title,
                accessibleDescription = related.title,
                testTag = "$LIBRARY_RELATED_TEST_TAG_PREFIX${related.itemId}",
                leadingIcon = TaffyIcon.Article,
                onClick = {
                    onIntent(
                        LibraryItemIntent.OpenRelated(related.collectionId, related.itemId),
                    )
                },
            )
        }
    }
}

@Composable
private fun LibraryItemActions(
    state: LibraryItemUiState,
    onIntent: (LibraryItemIntent) -> Unit,
) {
    TaffyDangerButton(
        label = taffyString(R.string.taffy_library_item_remove),
        onClick = { onIntent(LibraryItemIntent.RemoveItem) },
        enabled = state.canMutate,
        testTag = LIBRARY_ITEM_REMOVE_TEST_TAG,
    )
    if (!state.canMutate) {
        Text(
            text = taffyString(R.string.taffy_library_item_disconnected),
            style = TaffyTheme.typography.detail,
            color = TaffyTheme.colors.textSecondary,
        )
    }
}

/** The tags screen SCR-503's semantics tests name. */
const val LIBRARY_ITEM_CONFLICT_TEST_TAG: String = "library_item_conflict"
const val LIBRARY_ITEM_REMOVE_TEST_TAG: String = "library_item_remove"
const val LIBRARY_RELATED_TEST_TAG_PREFIX: String = "library_related_"

// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.workspaces

import androidx.compose.material3.Text
import androidx.compose.foundation.lazy.LazyListScope
import androidx.compose.foundation.lazy.items
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.ui.TaffyDangerButton
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyEmptyState
import com.taffygo.browser.ui.core.ui.TaffyIcon
import com.taffygo.browser.ui.core.ui.TaffyGroupedCard
import com.taffygo.browser.ui.core.ui.TaffyLazyScreen
import com.taffygo.browser.ui.core.ui.TaffyNavigator
import com.taffygo.browser.ui.core.ui.TaffySearchField
import com.taffygo.browser.ui.core.ui.TaffySecondaryButton
import com.taffygo.browser.ui.core.ui.screenViewModel
import com.taffygo.browser.ui.core.ui.taffyPlural
import com.taffygo.browser.ui.core.ui.taffyString

/**
 * Screen SCR-502 — one collection.
 *
 * A saved workspace is one collection, so export, rename, and remove use the
 * same exact revision and confirmation facts as Workspaces. Refresh first
 * shows the core's content-free source/work preview; only approval starts it.
 */
@Composable
fun LibraryCollectionScreen(
    destination: TaffyDestination.LibraryCollection,
    navigator: TaffyNavigator,
    modifier: Modifier = Modifier,
    selectedItemId: String? = null,
    showUp: Boolean = true,
) {
    val viewModel: LibraryCollectionViewModel = screenViewModel(destination)
    val state by viewModel.state.collectAsStateWithLifecycle()

    LaunchedEffect(Unit) { viewModel.onShown() }

    LibraryCollectionContent(
        state = state,
        onIntent = { viewModel.onIntent(it, navigator) },
        onBack = if (showUp) ({ navigator.goBack() }) else null,
        selectedItemId = selectedItemId,
        modifier = modifier,
    )
}

/** The stateless half. */
@Composable
fun LibraryCollectionContent(
    state: LibraryCollectionUiState,
    onIntent: (LibraryCollectionIntent) -> Unit,
    modifier: Modifier = Modifier,
    onBack: (() -> Unit)? = null,
    selectedItemId: String? = null,
) {
    var showRename by remember(state.collectionId, state.revision) { mutableStateOf(false) }
    var showDelete by remember(state.collectionId, state.revision) { mutableStateOf(false) }
    var showRefresh by remember(state.collectionId, state.refreshPreview?.previewId) {
        mutableStateOf(false)
    }
    // Keep the route heading distinct from the missing-state heading. Using
    // the same sentence for both makes screen readers announce the failure
    // twice without adding any information.
    val title = state.name.ifEmpty { taffyString(R.string.taffy_library_home_title) }
    val hasItems = !state.loading &&
        !state.unavailable &&
        !state.missing &&
        !state.isEmpty &&
        !state.hasNoMatches
    TaffyLazyScreen(
        destination = TaffyDestination.LibraryCollection(state.collectionId),
        title = title,
        onBack = onBack,
        modifier = modifier,
        listModifier = if (hasItems) {
            Modifier.testTag(LIBRARY_COLLECTION_LIST_TEST_TAG)
        } else {
            Modifier
        },
        footer = if (state.loading || state.unavailable || state.missing) {
            null
        } else {
            {
                LibraryCollectionActions(
                    state = state,
                    onIntent = onIntent,
                    onRefresh = { showRefresh = true },
                    onRename = { showRename = true },
                    onDelete = { showDelete = true },
                )
            }
        },
    ) {
        when {
            state.loading -> item(key = "collection-loading", contentType = "status") {
                LibrarySkeletonList(
                    description = taffyString(R.string.taffy_library_collection_loading),
                )
            }
            state.unavailable -> item(key = "collection-unavailable", contentType = "status") {
                TaffyEmptyState(
                    title = taffyString(R.string.taffy_library_unavailable_title),
                    body = taffyString(R.string.taffy_library_unavailable_body),
                    leading = { LibraryEmptyGlyph() },
                )
            }
            state.missing -> item(key = "collection-missing", contentType = "status") {
                TaffyEmptyState(
                    title = taffyString(R.string.taffy_library_collection_missing_title),
                    body = taffyString(R.string.taffy_library_collection_missing_body),
                    leading = { LibraryEmptyGlyph() },
                )
            }
            else -> libraryCollectionBody(
                state = state,
                selectedItemId = selectedItemId,
                onIntent = onIntent,
            )
        }
    }
    val refreshPreview = state.refreshPreview
    if (showRefresh && refreshPreview != null) {
        LibraryRefreshDialog(
            preview = refreshPreview,
            onDismiss = { showRefresh = false },
            onConfirm = {
                onIntent(LibraryCollectionIntent.ApproveRefresh(refreshPreview))
                showRefresh = false
            },
        )
    }
    if (showRename) {
        WorkspaceRenameDialog(
            currentName = state.name,
            onDismiss = { showRename = false },
            onConfirm = { name ->
                onIntent(LibraryCollectionIntent.RenameCollection(state.revision, name))
                showRename = false
            },
        )
    }
    val preview = state.deletionPreview
    if (showDelete && preview != null) {
        WorkspaceDeleteDialog(
            preview = preview,
            onDismiss = { showDelete = false },
            onConfirm = {
                onIntent(
                    LibraryCollectionIntent.DeleteCollection(
                        state.revision,
                        preview.confirmationToken,
                    ),
                )
                showDelete = false
            },
        )
    }
}

private fun LazyListScope.libraryCollectionBody(
    state: LibraryCollectionUiState,
    selectedItemId: String?,
    onIntent: (LibraryCollectionIntent) -> Unit,
) {
    item(key = "collection-search", contentType = "search") {
        TaffySearchField(
            value = state.query,
            onValueChange = { onIntent(LibraryCollectionIntent.QueryChanged(it)) },
            placeholder = taffyString(R.string.taffy_library_collection_search),
            testTag = LIBRARY_COLLECTION_SEARCH_TEST_TAG,
        )
    }
    if (state.conflictCount > 0) {
        item(key = "collection-conflicts", contentType = "status") {
            LibraryConflictMark(conflictCount = state.conflictCount)
        }
    }
    state.refreshResult?.let { result ->
        item(key = "collection-refresh-result", contentType = "status") {
            val changed = result.count(LibraryRepository.RefreshDisposition.CHANGED)
            val missing = result.count(LibraryRepository.RefreshDisposition.MISSING)
            val unchanged = result.count(LibraryRepository.RefreshDisposition.UNCHANGED)
            Text(
                text = taffyString(
                    R.string.taffy_library_refresh_result,
                    taffyPlural(R.plurals.taffy_library_refresh_changed_count, changed, changed),
                    taffyPlural(R.plurals.taffy_library_refresh_missing_count, missing, missing),
                    taffyPlural(
                        R.plurals.taffy_library_refresh_unchanged_count,
                        unchanged,
                        unchanged,
                    ),
                ),
                modifier = Modifier.testTag(LIBRARY_COLLECTION_REFRESH_RESULT_TEST_TAG),
                style = TaffyTheme.typography.detail,
                color = TaffyTheme.colors.textSecondary,
            )
        }
    }
    when {
        state.isEmpty -> item(key = "collection-empty", contentType = "status") {
            TaffyEmptyState(
                title = taffyString(R.string.taffy_library_collection_empty_title),
                body = taffyString(R.string.taffy_library_collection_empty_body),
                leading = { LibraryEmptyGlyph() },
            )
        }
        state.hasNoMatches -> item(key = "collection-no-matches", contentType = "status") {
            TaffyEmptyState(
                title = taffyString(R.string.taffy_library_collection_no_matches_title),
                body = taffyString(R.string.taffy_library_collection_no_matches_body),
                leading = { LibraryEmptyGlyph() },
            )
        }
        else -> items(
            items = state.items,
            key = LibraryRepository.Item::id,
            contentType = { "library-item" },
        ) { item ->
            TaffyGroupedCard {
                LibraryCollectionItemRow(
                    item = item,
                    selected = item.id == selectedItemId,
                    onOpen = { onIntent(LibraryCollectionIntent.OpenItem(item.id)) },
                )
            }
        }
    }
}

@Composable
private fun LibraryCollectionItemRow(
    item: LibraryRepository.Item,
    selected: Boolean,
    onOpen: () -> Unit,
) {
    val host = item.sources.firstOrNull()?.host
    val supporting = when {
        host != null && item.capturedAt != null ->
            taffyString(
                R.string.taffy_library_collection_item_supporting,
                host,
                item.capturedAt,
            )
        host != null -> host
        item.capturedAt != null -> taffyString(
            R.string.taffy_library_item_captured,
            item.capturedAt,
        )
        else -> null
    }
    val description = if (item.hasConflict) {
        taffyString(
            R.string.taffy_library_collection_item_description_conflict,
            item.title,
            supporting.orEmpty(),
            taffyString(R.string.taffy_library_item_conflict),
        )
    } else {
        taffyString(
            R.string.taffy_library_collection_item_description,
            item.title,
            supporting.orEmpty(),
        )
    }
    LibraryCardRow(
        title = item.title,
        supporting = supporting,
        accessibleDescription = description,
        selected = selected,
        testTag = "$LIBRARY_ITEM_TEST_TAG_PREFIX${item.id}",
        leadingIcon = TaffyIcon.Article,
        onClick = onOpen,
        trailing = {
            if (item.hasConflict) LibraryConflictMark(conflictCount = 1)
        },
    )
}

@Composable
private fun LibraryCollectionActions(
    state: LibraryCollectionUiState,
    onIntent: (LibraryCollectionIntent) -> Unit,
    onRefresh: () -> Unit,
    onRename: () -> Unit,
    onDelete: () -> Unit,
) {
    TaffySecondaryButton(
        label = taffyString(R.string.taffy_library_collection_refresh),
        onClick = onRefresh,
        enabled = state.canMutate && state.refreshPreview != null,
        testTag = LIBRARY_COLLECTION_REFRESH_TEST_TAG,
    )
    TaffySecondaryButton(
        label = taffyString(R.string.taffy_library_collection_export),
        onClick = { onIntent(LibraryCollectionIntent.Export) },
        enabled = state.canMutate,
        testTag = LIBRARY_COLLECTION_EXPORT_TEST_TAG,
    )
    if (state.canManage) {
        TaffySecondaryButton(
            label = taffyString(R.string.taffy_workspace_rename_action),
            onClick = onRename,
            testTag = LIBRARY_COLLECTION_RENAME_TEST_TAG,
        )
    }
    TaffyDangerButton(
        label = taffyString(R.string.taffy_library_collection_remove),
        onClick = onDelete,
        // Keep the promised action visible when the port is disconnected, but
        // require both management authority and its exact deletion preview
        // before it can open a confirmation.
        enabled = state.canManage && state.deletionPreview != null,
        testTag = LIBRARY_COLLECTION_REMOVE_TEST_TAG,
    )
    if (!state.canMutate) {
        Text(
            text = taffyString(R.string.taffy_library_collection_disconnected),
            style = TaffyTheme.typography.detail,
            color = TaffyTheme.colors.textSecondary,
        )
    } else if (state.refreshPreview == null) {
        Text(
            text = taffyString(R.string.taffy_library_collection_not_refreshable),
            style = TaffyTheme.typography.detail,
            color = TaffyTheme.colors.textSecondary,
        )
    }
}

/** The tags screen SCR-502's semantics tests name. */
const val LIBRARY_COLLECTION_SEARCH_TEST_TAG: String = "library_collection_search"
const val LIBRARY_COLLECTION_LIST_TEST_TAG: String = "library_collection_list"
const val LIBRARY_ITEM_TEST_TAG_PREFIX: String = "library_item_"
const val LIBRARY_COLLECTION_REFRESH_TEST_TAG: String = "library_collection_refresh"
const val LIBRARY_COLLECTION_REFRESH_RESULT_TEST_TAG: String = "library_collection_refresh_result"
const val LIBRARY_COLLECTION_EXPORT_TEST_TAG: String = "library_collection_export"
const val LIBRARY_COLLECTION_REMOVE_TEST_TAG: String = "library_collection_remove"
const val LIBRARY_COLLECTION_RENAME_TEST_TAG: String = "library_collection_rename"

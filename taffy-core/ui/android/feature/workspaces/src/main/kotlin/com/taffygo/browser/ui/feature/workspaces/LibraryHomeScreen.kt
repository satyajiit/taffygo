// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.workspaces

import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.foundation.lazy.LazyListScope
import androidx.compose.foundation.lazy.items
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyEmptyState
import com.taffygo.browser.ui.core.ui.TaffyIcon
import com.taffygo.browser.ui.core.ui.TaffyNavigator
import com.taffygo.browser.ui.core.ui.TaffyGroupedCard
import com.taffygo.browser.ui.core.ui.TaffyLazyScreen
import com.taffygo.browser.ui.core.ui.TaffySearchField
import com.taffygo.browser.ui.core.ui.screenViewModel
import com.taffygo.browser.ui.core.ui.taffyPlural
import com.taffygo.browser.ui.core.ui.taffyString

/**
 * Screen SCR-501 — Library home.
 *
 * Bookmarks, Memory, and History are named only in the empty teaching. None
 * of those piles are drawn here.
 */
@Composable
fun LibraryHomeScreen(
    navigator: TaffyNavigator,
    modifier: Modifier = Modifier,
    selectedCollectionId: String? = null,
    onBack: () -> Unit = { navigator.goBack() },
) {
    val viewModel: LibraryHomeViewModel = screenViewModel(TaffyDestination.LibraryHome)
    val state by viewModel.state.collectAsStateWithLifecycle()

    LaunchedEffect(Unit) { viewModel.onShown() }

    LibraryHomeContent(
        state = state,
        onIntent = { viewModel.onIntent(it, navigator) },
        onBack = onBack,
        selectedCollectionId = selectedCollectionId,
        modifier = modifier,
    )
}

/** The stateless half. */
@Composable
fun LibraryHomeContent(
    state: LibraryHomeUiState,
    onIntent: (LibraryHomeIntent) -> Unit,
    modifier: Modifier = Modifier,
    onBack: (() -> Unit)? = null,
    selectedCollectionId: String? = null,
) {
    val hasCollections = !state.loading &&
        !state.unavailable &&
        !state.isEmpty &&
        !state.hasNoMatches
    TaffyLazyScreen(
        destination = TaffyDestination.LibraryHome,
        title = taffyString(R.string.taffy_library_home_title),
        onBack = onBack,
        modifier = modifier,
        listModifier = if (hasCollections) {
            Modifier.testTag(LIBRARY_HOME_LIST_TEST_TAG)
        } else {
            Modifier
        },
    ) {
        item(key = "library-search", contentType = "search") {
            TaffySearchField(
                value = state.query,
                onValueChange = { onIntent(LibraryHomeIntent.QueryChanged(it)) },
                placeholder = taffyString(R.string.taffy_library_home_search),
                testTag = LIBRARY_HOME_SEARCH_TEST_TAG,
            )
        }
        when {
            state.loading -> item(key = "library-loading", contentType = "status") {
                LibrarySkeletonList(
                    description = taffyString(R.string.taffy_library_home_loading),
                )
            }
            state.unavailable -> item(key = "library-unavailable", contentType = "status") {
                TaffyEmptyState(
                    title = taffyString(R.string.taffy_library_unavailable_title),
                    body = taffyString(R.string.taffy_library_unavailable_body),
                    leading = { LibraryEmptyGlyph() },
                )
            }
            state.isEmpty -> item(key = "library-empty", contentType = "status") {
                TaffyEmptyState(
                    title = taffyString(R.string.taffy_library_home_empty_title),
                    body = taffyString(R.string.taffy_library_home_empty_body),
                    leading = { LibraryEmptyGlyph() },
                )
            }
            state.hasNoMatches -> item(key = "library-no-matches", contentType = "status") {
                TaffyEmptyState(
                    title = taffyString(R.string.taffy_library_home_no_matches_title),
                    body = taffyString(R.string.taffy_library_home_no_matches_body),
                    leading = { LibraryEmptyGlyph() },
                )
            }
            else -> libraryHomeCollections(
                collections = state.collections,
                selectedCollectionId = selectedCollectionId,
                onOpen = { onIntent(LibraryHomeIntent.OpenCollection(it)) },
            )
        }
    }
}

private fun LazyListScope.libraryHomeCollections(
    collections: List<LibraryRepository.Collection>,
    selectedCollectionId: String?,
    onOpen: (String) -> Unit,
) {
    items(
        items = collections,
        key = LibraryRepository.Collection::id,
        contentType = { "collection" },
    ) { collection ->
        val count = taffyPlural(
            R.plurals.taffy_library_item_count,
            collection.itemCount,
            collection.itemCount,
        )
        val supporting = collection.freshness?.let { freshness ->
            taffyString(R.string.taffy_library_home_supporting, count, freshness)
        } ?: count
        val conflicts = taffyPlural(
            R.plurals.taffy_library_conflict_count,
            collection.conflictCount,
            collection.conflictCount,
        )
        val description = if (collection.conflictCount > 0) {
            taffyString(
                R.string.taffy_library_home_description_conflict,
                collection.name,
                supporting,
                conflicts,
            )
        } else {
            taffyString(
                R.string.taffy_library_home_description,
                collection.name,
                supporting,
            )
        }
        TaffyGroupedCard {
            LibraryCardRow(
                title = collection.name,
                supporting = supporting,
                accessibleDescription = description,
                selected = collection.id == selectedCollectionId,
                testTag = "$LIBRARY_COLLECTION_TEST_TAG_PREFIX${collection.id}",
                leadingIcon = TaffyIcon.Books,
                onClick = { onOpen(collection.id) },
                trailing = { LibraryConflictMark(collection.conflictCount) },
            )
        }
    }
}

/** The tags screen SCR-501's semantics tests name. */
const val LIBRARY_HOME_SEARCH_TEST_TAG: String = "library_home_search"
const val LIBRARY_HOME_LIST_TEST_TAG: String = "library_home_list"
const val LIBRARY_COLLECTION_TEST_TAG_PREFIX: String = "library_collection_"

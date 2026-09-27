// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.workspaces

import androidx.compose.foundation.layout.size
import androidx.compose.material3.Icon
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyIcon
import com.taffygo.browser.ui.core.ui.TaffyNavigator
import com.taffygo.browser.ui.core.ui.TaffyPrimaryButton
import com.taffygo.browser.ui.core.ui.TaffyScreen
import com.taffygo.browser.ui.core.ui.TaffySectionHeader
import com.taffygo.browser.ui.core.ui.screenViewModel
import com.taffygo.browser.ui.core.ui.taffyPlural
import com.taffygo.browser.ui.core.ui.taffyString

/**
 * Screen SCR-504 — Keep this.
 *
 * The person chooses a collection and what would be kept. The Keep control
 * stays off until a port can actually add something, so this screen never
 * claims Library gained an item.
 */
@Composable
fun KeepThisScreen(
    navigator: TaffyNavigator,
    modifier: Modifier = Modifier,
    showUp: Boolean = true,
) {
    val viewModel: KeepThisViewModel = screenViewModel(TaffyDestination.KeepThis)
    val state by viewModel.state.collectAsStateWithLifecycle()

    LaunchedEffect(Unit) { viewModel.onShown() }

    KeepThisContent(
        state = state,
        onIntent = { viewModel.onIntent(it, navigator) },
        onBack = if (showUp) {
            { viewModel.onIntent(KeepThisIntent.Dismiss, navigator) }
        } else {
            null
        },
        modifier = modifier,
    )
}

/** The stateless half. */
@Composable
fun KeepThisContent(
    state: KeepThisUiState,
    onIntent: (KeepThisIntent) -> Unit,
    modifier: Modifier = Modifier,
    onBack: (() -> Unit)? = null,
) {
    TaffyScreen(
        destination = TaffyDestination.KeepThis,
        title = taffyString(R.string.taffy_library_keep_title),
        onBack = onBack,
        modifier = modifier,
        footer = {
            if (!state.canKeep) {
                Text(
                    text = taffyString(R.string.taffy_library_keep_disconnected),
                    style = TaffyTheme.typography.detail,
                    color = TaffyTheme.colors.textSecondary,
                )
            }
            TaffyPrimaryButton(
                label = taffyString(R.string.taffy_library_keep_action),
                onClick = { onIntent(KeepThisIntent.Keep) },
                enabled = state.keepEnabled,
                testTag = KEEP_THIS_ACTION_TEST_TAG,
            )
        },
    ) {
        Text(
            text = taffyString(R.string.taffy_library_keep_body),
            style = TaffyTheme.typography.detail,
            color = TaffyTheme.colors.textSecondary,
        )
        TaffySectionHeader(title = taffyString(R.string.taffy_library_keep_collections))
        KeepThisCollections(state = state, onIntent = onIntent)
        TaffySectionHeader(title = taffyString(R.string.taffy_library_keep_what))
        KeepThisKinds(state = state, onIntent = onIntent)
    }
}

@Composable
private fun KeepThisCollections(
    state: KeepThisUiState,
    onIntent: (KeepThisIntent) -> Unit,
) {
    when {
        state.loading -> LibrarySkeletonList(
            description = taffyString(R.string.taffy_library_keep_loading),
        )
        state.collections.isEmpty() -> Text(
            text = taffyString(R.string.taffy_library_keep_no_collections),
            style = TaffyTheme.typography.detail,
            color = TaffyTheme.colors.textSecondary,
        )
        else -> LibraryGroupedItems(
            count = state.collections.size,
            testTag = KEEP_THIS_COLLECTIONS_TEST_TAG,
        ) { index ->
            val collection = state.collections[index]
            val count = taffyPlural(
                R.plurals.taffy_library_item_count,
                collection.itemCount,
                collection.itemCount,
            )
            val chosen = collection.id == state.selectedCollectionId
            LibraryCardRow(
                title = collection.name,
                supporting = count,
                accessibleDescription = taffyString(
                    R.string.taffy_library_keep_collection_description,
                    collection.name,
                    count,
                ),
                selected = chosen,
                testTag = "$KEEP_THIS_COLLECTION_TEST_TAG_PREFIX${collection.id}",
                leadingIcon = TaffyIcon.Books,
                onClick = { onIntent(KeepThisIntent.SelectCollection(collection.id)) },
                trailing = { if (chosen) KeepThisChosenMark() },
            )
        }
    }
}

@Composable
private fun KeepThisKinds(
    state: KeepThisUiState,
    onIntent: (KeepThisIntent) -> Unit,
) {
    LibraryGroupedItems(
        count = state.kinds.size,
        testTag = KEEP_THIS_KINDS_TEST_TAG,
    ) { index ->
        val kind = state.kinds[index]
        val label = taffyString(keepKindLabel(kind))
        val chosen = kind == state.selectedKind
        LibraryCardRow(
            title = label,
            accessibleDescription = taffyString(
                R.string.taffy_library_keep_kind_description,
                label,
            ),
            selected = chosen,
            testTag = "$KEEP_THIS_KIND_TEST_TAG_PREFIX${kind.name.lowercase()}",
            leadingIcon = keepKindIcon(kind),
            onClick = { onIntent(KeepThisIntent.SelectKind(kind)) },
            trailing = { if (chosen) KeepThisChosenMark() },
        )
    }
}

@Composable
private fun KeepThisChosenMark() {
    Icon(
        imageVector = TaffyIcon.Check,
        contentDescription = null,
        tint = TaffyTheme.colors.textPrimary,
        modifier = Modifier.size(20.dp),
    )
}

internal fun keepKindLabel(kind: KeepThisKind): Int = when (kind) {
    KeepThisKind.FACT -> R.string.taffy_library_keep_kind_fact
    KeepThisKind.PAGE_EXTRACT -> R.string.taffy_library_keep_kind_page
    KeepThisKind.FILE -> R.string.taffy_library_keep_kind_file
    KeepThisKind.WORKSPACE -> R.string.taffy_library_keep_kind_workspace
}

private fun keepKindIcon(kind: KeepThisKind) = when (kind) {
    KeepThisKind.FACT, KeepThisKind.PAGE_EXTRACT -> TaffyIcon.Article
    KeepThisKind.FILE -> TaffyIcon.DownloadSimple
    KeepThisKind.WORKSPACE -> TaffyIcon.Table
}

/** The tags screen SCR-504's semantics tests name. */
const val KEEP_THIS_ACTION_TEST_TAG: String = "keep_this_action"
const val KEEP_THIS_COLLECTIONS_TEST_TAG: String = "keep_this_collections"
const val KEEP_THIS_KINDS_TEST_TAG: String = "keep_this_kinds"
const val KEEP_THIS_COLLECTION_TEST_TAG_PREFIX: String = "keep_this_collection_"
const val KEEP_THIS_KIND_TEST_TAG_PREFIX: String = "keep_this_kind_"

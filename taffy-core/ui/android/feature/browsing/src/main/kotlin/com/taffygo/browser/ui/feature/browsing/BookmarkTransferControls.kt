// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.ui.TaffyInfoTile
import com.taffygo.browser.ui.core.ui.TaffySecondaryButton
import com.taffygo.browser.ui.core.ui.taffyString

/** The complete portable-transfer surface behind two labelled controls. */
@Composable
internal fun BookmarkTransferControls(
    state: BookmarksUiState,
    onIntent: (BookmarksIntent) -> Unit,
) {
    Row(
        modifier = Modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.tight),
    ) {
        TaffySecondaryButton(
            label = taffyString(R.string.taffy_pages_bookmarks_import),
            onClick = { onIntent(BookmarksIntent.RequestImport) },
            enabled = state.canTransfer,
            loading = state.transferStatus.isImportRunning(),
            testTag = BOOKMARKS_IMPORT_TEST_TAG,
            modifier = Modifier.weight(1f),
        )
        TaffySecondaryButton(
            label = taffyString(R.string.taffy_pages_bookmarks_export),
            onClick = { onIntent(BookmarksIntent.RequestExport) },
            enabled = state.canTransfer,
            loading = state.transferStatus == BookmarksUiState.TransferStatus.WRITING_EXPORT,
            testTag = BOOKMARKS_EXPORT_TEST_TAG,
            modifier = Modifier.weight(1f),
        )
    }
}

@Composable
internal fun BookmarkTransferNotice(state: BookmarksUiState) {
    val text = when (state.transferStatus) {
        BookmarksUiState.TransferStatus.IMPORTED -> state.importResult?.let { result ->
            taffyString(
                R.string.taffy_pages_bookmarks_imported,
                result.imported,
                result.duplicates,
                result.rejected,
            )
        }
        BookmarksUiState.TransferStatus.EXPORTED ->
            taffyString(R.string.taffy_pages_bookmarks_exported)
        BookmarksUiState.TransferStatus.FAILED ->
            taffyString(R.string.taffy_pages_bookmarks_transfer_failed)
        else -> null
    } ?: return
    TaffyInfoTile(testTag = BOOKMARKS_TRANSFER_STATUS_TEST_TAG) {
        Text(
            text = text,
            style = TaffyTheme.typography.detail,
            color = TaffyTheme.colors.textSecondary,
        )
    }
}

private fun BookmarksUiState.TransferStatus.isImportRunning(): Boolean = when (this) {
    BookmarksUiState.TransferStatus.READING_IMPORT,
    BookmarksUiState.TransferStatus.IMPORTING,
    -> true
    else -> false
}

const val BOOKMARKS_IMPORT_TEST_TAG: String = "bookmarks_import"
const val BOOKMARKS_EXPORT_TEST_TAG: String = "bookmarks_export"
const val BOOKMARKS_TRANSFER_STATUS_TEST_TAG: String = "bookmarks_transfer_status"


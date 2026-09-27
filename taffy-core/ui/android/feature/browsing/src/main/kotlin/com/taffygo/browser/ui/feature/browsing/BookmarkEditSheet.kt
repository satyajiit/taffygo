// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.text.BasicTextField
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.SolidColor
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import com.taffygo.browser.ui.core.designsystem.TaffyBorders
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.ui.TaffyBottomSheet
import com.taffygo.browser.ui.core.ui.TaffyDangerButton
import com.taffygo.browser.ui.core.ui.TaffyPrimaryButton
import com.taffygo.browser.ui.core.ui.TaffySecondaryButton
import com.taffygo.browser.ui.core.ui.taffyString

/** Editor for one starred page: name, folder, save, and a labelled delete. */
@Composable
internal fun BookmarkEditSheet(
    bookmark: Bookmark,
    folders: List<BookmarkFolder>,
    onSave: (String, BookmarkFolder.Id) -> Unit,
    onDelete: () -> Unit,
    onDismiss: () -> Unit,
) {
    var title by remember(bookmark.id) { mutableStateOf(bookmark.title) }
    var folderId by remember(bookmark.id) { mutableStateOf(bookmark.folderId) }
    val namedFolders = folders.filter { it.bookmarks.isNotEmpty() || it.id == folderId }
    val allLabel = taffyString(R.string.taffy_pages_bookmarks_folder_all)

    TaffyBottomSheet(
        title = taffyString(R.string.taffy_pages_bookmarks_edit_title),
        onDismissRequest = onDismiss,
        testTag = BOOKMARK_EDIT_TEST_TAG,
    ) {
        Text(
            text = taffyString(R.string.taffy_pages_bookmarks_edit_name),
            style = TaffyTheme.typography.detail,
            color = TaffyTheme.colors.textSecondary,
        )
        BookmarkNameField(
            value = title,
            onValueChange = { title = it },
            placeholder = bookmark.host,
        )
        if (namedFolders.size > 1) {
            Text(
                text = taffyString(R.string.taffy_pages_bookmarks_edit_folder),
                style = TaffyTheme.typography.detail,
                color = TaffyTheme.colors.textSecondary,
            )
            namedFolders.forEach { folder ->
                val label = bookmarkFolderTitle(folder, allLabel)
                TaffySecondaryButton(
                    label = label,
                    onClick = { folderId = folder.id },
                    enabled = folder.id != folderId,
                    testTag = "$BOOKMARK_FOLDER_CHOICE_TEST_TAG_PREFIX${folder.id.value}",
                )
            }
        }
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.tight),
        ) {
            TaffySecondaryButton(
                label = taffyString(R.string.taffy_pages_cancel),
                onClick = onDismiss,
                testTag = BOOKMARK_EDIT_CANCEL_TEST_TAG,
                modifier = Modifier.weight(1f),
            )
            TaffyPrimaryButton(
                label = taffyString(R.string.taffy_pages_bookmarks_save),
                onClick = { onSave(title, folderId) },
                testTag = BOOKMARK_EDIT_SAVE_TEST_TAG,
                modifier = Modifier.weight(1f),
            )
        }
        TaffyDangerButton(
            label = taffyString(R.string.taffy_pages_bookmarks_delete),
            onClick = onDelete,
            testTag = BOOKMARK_EDIT_DELETE_TEST_TAG,
        )
    }
}

@Composable
private fun BookmarkNameField(
    value: String,
    onValueChange: (String) -> Unit,
    placeholder: String,
) {
    val spoken = taffyString(R.string.taffy_pages_bookmarks_edit_name)
    val shape = TaffyTheme.shapes.row
    BasicTextField(
        value = value,
        onValueChange = onValueChange,
        singleLine = true,
        textStyle = TaffyTheme.typography.body.copy(color = TaffyTheme.colors.textPrimary),
        cursorBrush = SolidColor(TaffyTheme.colors.textPrimary),
        modifier = Modifier
            .fillMaxWidth()
            .heightIn(min = TaffyTheme.spacing.minimumTouchTarget)
            .clip(shape)
            .background(TaffyTheme.colors.surfaceSunken)
            .border(TaffyBorders.standard, TaffyTheme.colors.outline, shape)
            .padding(horizontal = TaffyTheme.spacing.snug, vertical = TaffyTheme.spacing.tight)
            .testTag(BOOKMARK_EDIT_NAME_TEST_TAG)
            .semantics { contentDescription = spoken },
        decorationBox = { field ->
            if (value.isEmpty()) {
                Text(
                    text = placeholder,
                    style = TaffyTheme.typography.body,
                    color = TaffyTheme.colors.textSecondary,
                )
            }
            field()
        },
    )
}

internal fun bookmarkFolderTitle(folder: BookmarkFolder, allLabel: String): String =
    if (folder.id == BookmarkFolder.Id.ALL) allLabel else folder.name.ifBlank { allLabel }

internal const val BOOKMARK_EDIT_TEST_TAG: String = "bookmark_edit"
internal const val BOOKMARK_EDIT_NAME_TEST_TAG: String = "bookmark_edit_name"
internal const val BOOKMARK_EDIT_SAVE_TEST_TAG: String = "bookmark_edit_save"
internal const val BOOKMARK_EDIT_CANCEL_TEST_TAG: String = "bookmark_edit_cancel"
internal const val BOOKMARK_EDIT_DELETE_TEST_TAG: String = "bookmark_edit_delete"
internal const val BOOKMARK_FOLDER_CHOICE_TEST_TAG_PREFIX: String = "bookmark_folder_choice_"

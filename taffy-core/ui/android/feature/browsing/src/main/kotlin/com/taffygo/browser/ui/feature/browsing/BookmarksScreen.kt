// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material3.Icon
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyEmptyState
import com.taffygo.browser.ui.core.ui.TaffyGroupedCard
import com.taffygo.browser.ui.core.ui.TaffyIcon
import com.taffygo.browser.ui.core.ui.TaffyIconButton
import com.taffygo.browser.ui.core.ui.LocalAppDispatchers
import com.taffygo.browser.ui.core.ui.TaffyNavigator
import com.taffygo.browser.ui.core.ui.TaffyScreen
import com.taffygo.browser.ui.core.ui.TaffySearchField
import com.taffygo.browser.ui.core.ui.TaffySectionHeader
import com.taffygo.browser.ui.core.ui.screenViewModel
import com.taffygo.browser.ui.core.ui.taffyString
import kotlinx.coroutines.launch

/**
 * Screen SCR-202 — pages the person starred.
 *
 * Not Library, not Memory, and not a reading list. Portable import/export
 * uses Android's document picker. Delete is a labelled button as well as a swipe.
 */
@Composable
fun BookmarksScreen(
    navigator: TaffyNavigator,
    modifier: Modifier = Modifier,
) {
    val viewModel: BookmarksViewModel = screenViewModel(TaffyDestination.Bookmarks)
    val state by viewModel.state.collectAsStateWithLifecycle()
    val context = LocalContext.current.applicationContext
    val ioDispatcher = LocalAppDispatchers.current.io
    val scope = rememberCoroutineScope()
    val platform = remember(context, ioDispatcher) {
        BookmarkTransferPlatform(context.contentResolver, ioDispatcher)
    }
    val openDocument = rememberLauncherForActivityResult(OpenBookmarkDocument()) { uri ->
        if (uri == null) {
            viewModel.onIntent(BookmarksIntent.ImportPickerCancelled, navigator)
        } else if (viewModel.importDestinationSelected()) {
            scope.launch { viewModel.importDocument(platform.read(uri)) }
        }
    }
    val createDocument = rememberLauncherForActivityResult(CreateBookmarkDocument()) { uri ->
        if (uri == null) {
            viewModel.onIntent(BookmarksIntent.ExportPickerCancelled, navigator)
        } else {
            viewModel.writeExport { document -> platform.write(uri, document) }
        }
    }

    LaunchedEffect(Unit) { viewModel.onShown() }

    BookmarksContent(
        state = state,
        onIntent = { intent ->
            when (intent) {
                BookmarksIntent.RequestImport -> {
                    if (state.canTransfer) {
                        viewModel.onIntent(intent, navigator)
                        try {
                            openDocument.launch(Unit)
                        } catch (_: RuntimeException) {
                            viewModel.onIntent(BookmarksIntent.TransferFailed, navigator)
                        }
                    }
                }
                BookmarksIntent.RequestExport -> {
                    if (state.canTransfer) {
                        viewModel.onIntent(intent, navigator)
                        try {
                            createDocument.launch(Unit)
                        } catch (_: RuntimeException) {
                            viewModel.onIntent(BookmarksIntent.TransferFailed, navigator)
                        }
                    }
                }
                else -> viewModel.onIntent(intent, navigator)
            }
        },
        onBack = { viewModel.onIntent(BookmarksIntent.Dismiss, navigator) },
        modifier = modifier,
    )
}

/** The stateless half, which is what a preview and a semantics test render. */
@Composable
fun BookmarksContent(
    state: BookmarksUiState,
    onIntent: (BookmarksIntent) -> Unit,
    modifier: Modifier = Modifier,
    onBack: (() -> Unit)? = null,
) {
    var pendingDelete by remember { mutableStateOf<Bookmark?>(null) }
    val allLabel = taffyString(R.string.taffy_pages_bookmarks_folder_all)
    val folderSubtitle = state.currentFolder?.let { bookmarkFolderTitle(it, allLabel) }

    TaffyScreen(
        destination = TaffyDestination.Bookmarks,
        title = taffyString(R.string.taffy_pages_bookmarks_title),
        subtitle = folderSubtitle,
        onBack = onBack,
        modifier = modifier,
        scrollable = state.visibleCount == 0,
        header = {
            Column(verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.tight)) {
                TaffySearchField(
                    value = state.query,
                    onValueChange = { onIntent(BookmarksIntent.QueryChanged(it)) },
                    placeholder = taffyString(R.string.taffy_pages_bookmarks_search),
                    testTag = BOOKMARKS_SEARCH_TEST_TAG,
                )
                BookmarkTransferControls(state, onIntent)
            }
        },
    ) {
        BookmarkTransferNotice(state)
        when {
            state.isLoading -> PagesRecordSkeleton(
                rows = BOOKMARKS_SKELETON_ROWS,
                firstDescription = taffyString(R.string.taffy_pages_bookmarks_loading),
                rowHeight = BOOKMARKS_ROW_HEIGHT,
                testTag = BOOKMARKS_LOADING_TEST_TAG,
            )
            state.isUnavailable -> TaffyEmptyState(
                title = taffyString(R.string.taffy_pages_bookmarks_unavailable_title),
                body = taffyString(R.string.taffy_pages_bookmarks_unavailable_body),
                leading = { BookmarksEmptyGlyph() },
            )
            state.isEmpty -> TaffyEmptyState(
                title = taffyString(R.string.taffy_pages_bookmarks_empty_title),
                body = taffyString(R.string.taffy_pages_bookmarks_empty_body),
                leading = { BookmarksEmptyGlyph() },
            )
            state.hasNoMatches -> TaffyEmptyState(
                title = taffyString(R.string.taffy_pages_bookmarks_no_matches_title),
                body = taffyString(R.string.taffy_pages_bookmarks_no_matches_body),
            )
            else -> BookmarkFolders(
                state = state,
                allLabel = allLabel,
                onIntent = onIntent,
                onAskDelete = { pendingDelete = it },
            )
        }
    }

    state.editing?.let { bookmark ->
        BookmarkEditSheet(
            bookmark = bookmark,
            folders = state.folders,
            onSave = { title, folderId ->
                onIntent(BookmarksIntent.SaveEdit(title, folderId))
            },
            onDelete = { pendingDelete = bookmark },
            onDismiss = { onIntent(BookmarksIntent.DismissEdit) },
        )
    }

    pendingDelete?.let { bookmark ->
        val title = bookmark.title.ifBlank { bookmark.host }
        PagesConfirmSheet(
            title = taffyString(R.string.taffy_pages_bookmarks_delete_confirm_title),
            body = taffyString(
                R.string.taffy_pages_bookmarks_delete_confirm_body,
                title,
                bookmark.host,
            ),
            confirmLabel = taffyString(R.string.taffy_pages_remove),
            cancelLabel = taffyString(R.string.taffy_pages_cancel),
            onConfirm = {
                onIntent(BookmarksIntent.Delete(bookmark.id))
                pendingDelete = null
            },
            onDismiss = { pendingDelete = null },
            testTag = BOOKMARKS_CONFIRM_TEST_TAG,
        )
    }
}

@Composable
private fun BookmarkFolders(
    state: BookmarksUiState,
    allLabel: String,
    onIntent: (BookmarksIntent) -> Unit,
    onAskDelete: (Bookmark) -> Unit,
) {
    val visible = state.folders.filter { it.bookmarks.isNotEmpty() }
    LazyColumn(
        verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.snug),
        modifier = Modifier
            .fillMaxSize()
            .testTag(BOOKMARKS_LIST_TEST_TAG),
    ) {
        visible.forEach { folder ->
            item(key = "folder-${folder.id.value}", contentType = "folder") {
                val label = bookmarkFolderTitle(folder, allLabel)
                val canOpenFolder = state.currentFolderId == null && visible.size > 1
                val folderDescription = taffyString(
                    R.string.taffy_pages_bookmarks_folder_description,
                    label,
                )
                TaffySectionHeader(
                    title = label,
                    modifier = Modifier
                        .testTag("$BOOKMARKS_FOLDER_TEST_TAG_PREFIX${folder.id.value}")
                        .then(
                            if (canOpenFolder) {
                                Modifier
                                    .clickable {
                                        onIntent(BookmarksIntent.OpenFolder(folder.id))
                                    }
                                    .semantics { contentDescription = folderDescription }
                            } else {
                                Modifier
                            },
                        ),
                )
            }
            items(
                items = folder.bookmarks,
                key = { it.id.value },
                contentType = { "bookmark" },
            ) { bookmark ->
                TaffyGroupedCard {
                    BookmarkRow(
                        bookmark = bookmark,
                        mark = state.siteMarks[bookmark.host],
                        canOpen = state.canOpen,
                        onOpen = { onIntent(BookmarksIntent.Open(bookmark.id)) },
                        onEdit = { onIntent(BookmarksIntent.Edit(bookmark.id)) },
                        onAskDelete = { onAskDelete(bookmark) },
                    )
                }
            }
        }
    }
}

@Composable
private fun BookmarkRow(
    bookmark: Bookmark,
    mark: android.graphics.Bitmap?,
    canOpen: Boolean,
    onOpen: () -> Unit,
    onEdit: () -> Unit,
    onAskDelete: () -> Unit,
) {
    val title = bookmark.title.ifBlank { bookmark.host }
    val description = taffyString(
        R.string.taffy_pages_bookmarks_row_description,
        title,
        bookmark.host,
    )
    PagesSwipeDelete(enabled = true, onSwiped = onAskDelete) {
        PagesRecordRow(
            title = title,
            supporting = bookmark.host,
            accessibleDescription = description,
            testTag = "$BOOKMARK_TEST_TAG_PREFIX${bookmark.id.value}",
            minHeight = BOOKMARKS_ROW_HEIGHT,
            onClick = onOpen.takeIf { canOpen },
            leading = { PagesSiteMark(host = bookmark.host, mark = mark) },
            trailing = {
                Row(
                    horizontalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.step),
                    verticalAlignment = Alignment.CenterVertically,
                ) {
                    TaffyIconButton(
                        icon = TaffyIcon.PencilSimple,
                        contentDescription = taffyString(R.string.taffy_pages_bookmarks_edit),
                        onClick = onEdit,
                        testTag = "$BOOKMARK_EDIT_ROW_TEST_TAG_PREFIX${bookmark.id.value}",
                    )
                    TaffyIconButton(
                        icon = TaffyIcon.Trash,
                        contentDescription = taffyString(R.string.taffy_pages_bookmarks_delete),
                        onClick = onAskDelete,
                        testTag = "$BOOKMARK_DELETE_TEST_TAG_PREFIX${bookmark.id.value}",
                    )
                }
            },
        )
    }
}

@Composable
private fun BookmarksEmptyGlyph() {
    Icon(
        imageVector = TaffyIcon.BookmarkSimple,
        contentDescription = null,
        tint = TaffyTheme.colors.textSecondary,
    )
}

/** The tags screen SCR-202's semantics tests name. */
const val BOOKMARKS_SEARCH_TEST_TAG: String = "bookmarks_search"
const val BOOKMARKS_LIST_TEST_TAG: String = "bookmarks_list"
const val BOOKMARKS_LOADING_TEST_TAG: String = "bookmarks_loading"
const val BOOKMARKS_CONFIRM_TEST_TAG: String = "bookmarks_delete_confirm"
const val BOOKMARKS_FOLDER_TEST_TAG_PREFIX: String = "bookmarks_folder_"
const val BOOKMARK_TEST_TAG_PREFIX: String = "bookmark_"
const val BOOKMARK_EDIT_ROW_TEST_TAG_PREFIX: String = "bookmark_edit_"
const val BOOKMARK_DELETE_TEST_TAG_PREFIX: String = "bookmark_delete_"

private const val BOOKMARKS_SKELETON_ROWS = 4
private val BOOKMARKS_ROW_HEIGHT = 48.dp

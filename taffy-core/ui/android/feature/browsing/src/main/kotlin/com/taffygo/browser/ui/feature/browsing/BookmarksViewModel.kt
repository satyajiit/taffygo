// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import androidx.lifecycle.SavedStateHandle
import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.taffygo.browser.ui.core.analytics.AnalyticsClient
import com.taffygo.browser.ui.core.analytics.AnalyticsEvent
import com.taffygo.browser.ui.core.browser.BrowserRepository
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyNavigator
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.flow.launchIn
import kotlinx.coroutines.flow.onEach
import kotlinx.coroutines.flow.stateIn
import kotlinx.coroutines.launch

/** Screen SCR-202's one source of truth. */
class BookmarksViewModel(
    private val bookmarks: BookmarksRepository,
    private val browser: BrowserRepository,
    private val analytics: AnalyticsClient,
    private val savedState: SavedStateHandle,
) : ViewModel() {

    private val query = MutableStateFlow(savedState.get<String>(QUERY_KEY).orEmpty())
    private val currentFolderId = MutableStateFlow(savedFolderId())
    private val editing = MutableStateFlow<Bookmark?>(null)
    private val transfer = MutableStateFlow(TransferPresentation())

    private val bookmarksProjection: StateFlow<BookmarksUiState> =
        combine(
            bookmarks.snapshot,
            query,
            currentFolderId,
            editing,
            browser.siteMarks,
        ) { snapshot, queryText, folderId, editingBookmark, marks ->
            projectBookmarks(snapshot, queryText, folderId, editingBookmark).copy(siteMarks = marks)
        }
            .stateIn(
                scope = viewModelScope,
                started = SharingStarted.WhileSubscribed(SUBSCRIPTION_TIMEOUT_MILLIS),
                initialValue = projectBookmarks(
                    bookmarks.snapshot.value,
                    query.value,
                    currentFolderId.value,
                    editing.value,
                ).copy(siteMarks = browser.siteMarks.value),
            )

    /** What screen SCR-202 renders. */
    val state: StateFlow<BookmarksUiState> =
        combine(
            bookmarksProjection,
            transfer,
        ) { projection, presentation ->
            projection.copy(
                transferStatus = presentation.status,
                importResult = presentation.importResult,
            )
        }
            .stateIn(
                scope = viewModelScope,
                started = SharingStarted.WhileSubscribed(SUBSCRIPTION_TIMEOUT_MILLIS),
                initialValue = bookmarksProjection.value,
            )

    init {
        bookmarks.snapshot
            .onEach { snapshot ->
                val ready = snapshot as? BookmarksSnapshot.Ready ?: return@onEach
                browser.requestSiteMarks(ready.folders.flatMap { folder -> folder.bookmarks.map { it.host } })
            }
            .launchIn(viewModelScope)
    }

    /** Act on something the user did. */
    fun onIntent(intent: BookmarksIntent, navigator: TaffyNavigator) {
        when (intent) {
            is BookmarksIntent.QueryChanged -> {
                query.value = intent.query
                savedState[QUERY_KEY] = intent.query
            }
            is BookmarksIntent.Open -> viewModelScope.launch { open(intent.id, navigator) }
            is BookmarksIntent.Edit -> editing.value = bookmark(intent.id)
            BookmarksIntent.RequestImport -> beginTransfer(
                BookmarksUiState.TransferStatus.CHOOSING_IMPORT,
            )
            BookmarksIntent.RequestExport -> beginTransfer(
                BookmarksUiState.TransferStatus.CHOOSING_EXPORT,
            )
            BookmarksIntent.ImportPickerCancelled,
            BookmarksIntent.ExportPickerCancelled,
            -> transfer.value = TransferPresentation()
            BookmarksIntent.TransferFailed -> transfer.value =
                TransferPresentation(BookmarksUiState.TransferStatus.FAILED)
            is BookmarksIntent.SaveEdit -> viewModelScope.launch {
                val id = editing.value?.id ?: return@launch
                bookmarks.edit(id, intent.title, intent.folderId)
                editing.value = null
            }
            BookmarksIntent.DismissEdit -> editing.value = null
            is BookmarksIntent.Delete -> viewModelScope.launch {
                bookmarks.delete(intent.id)
                if (editing.value?.id == intent.id) editing.value = null
            }
            is BookmarksIntent.OpenFolder -> {
                currentFolderId.value = intent.id
                savedState[FOLDER_KEY] = intent.id.value
            }
            BookmarksIntent.Dismiss -> {
                if (currentFolderId.value != null) {
                    currentFolderId.value = null
                    savedState[FOLDER_KEY] = null
                } else {
                    navigator.goBack()
                }
            }
        }
    }

    /** Decode has moved off the UI thread; merge the already bounded tree now. */
    fun importDocument(document: BookmarkTransferDocument?) {
        if (transfer.value.status != BookmarksUiState.TransferStatus.READING_IMPORT) return
        if (document == null) {
            transfer.value = TransferPresentation(BookmarksUiState.TransferStatus.FAILED)
            return
        }
        transfer.value = TransferPresentation(BookmarksUiState.TransferStatus.IMPORTING)
        viewModelScope.launch {
            val result = bookmarks.importDocument(document)
            transfer.value = if (result == null) {
                TransferPresentation(BookmarksUiState.TransferStatus.FAILED)
            } else {
                TransferPresentation(BookmarksUiState.TransferStatus.IMPORTED, result)
            }
        }
    }

    /** The URI grant is live; let the trusted platform reader consume it. */
    fun importDestinationSelected(): Boolean {
        if (transfer.value.status != BookmarksUiState.TransferStatus.CHOOSING_IMPORT) return false
        transfer.value = TransferPresentation(BookmarksUiState.TransferStatus.READING_IMPORT)
        return true
    }

    /** Render the live profile then give that immutable tree to one trusted writer. */
    fun writeExport(write: suspend (BookmarkTransferDocument) -> Boolean) {
        if (transfer.value.status != BookmarksUiState.TransferStatus.CHOOSING_EXPORT) return
        transfer.value = TransferPresentation(BookmarksUiState.TransferStatus.WRITING_EXPORT)
        viewModelScope.launch {
            val document = bookmarks.exportDocument()
            val written = document != null && write(document)
            transfer.value = TransferPresentation(
                if (written) {
                    BookmarksUiState.TransferStatus.EXPORTED
                } else {
                    BookmarksUiState.TransferStatus.FAILED
                },
            )
        }
    }

    /** Record that this screen was shown. */
    fun onShown() {
        analytics.record(AnalyticsEvent.ScreenShown(TaffyDestination.Bookmarks.screenId))
    }

    private fun savedFolderId(): BookmarkFolder.Id? {
        val raw = savedState.get<String>(FOLDER_KEY)?.takeIf { it.isNotBlank() } ?: return null
        return BookmarkFolder.Id(raw)
    }

    private fun bookmark(id: Bookmark.Id): Bookmark? {
        val ready = bookmarks.snapshot.value as? BookmarksSnapshot.Ready ?: return null
        return ready.folders.asSequence().flatMap { it.bookmarks }.firstOrNull { it.id == id }
    }

    private suspend fun open(id: Bookmark.Id, navigator: TaffyNavigator) {
        if (!bookmarks.open(id)) return
        navigator.replaceCurrent(TaffyDestination.BrowserMain)
    }

    private fun beginTransfer(status: BookmarksUiState.TransferStatus) {
        if (!state.value.canTransfer) return
        transfer.value = TransferPresentation(status)
    }

    private data class TransferPresentation(
        val status: BookmarksUiState.TransferStatus = BookmarksUiState.TransferStatus.IDLE,
        val importResult: BookmarkTransferDocument.ImportResult? = null,
    )

    private companion object {
        const val SUBSCRIPTION_TIMEOUT_MILLIS = 5_000L
        const val QUERY_KEY = "bookmarks_query"
        const val FOLDER_KEY = "bookmarks_folder"
    }
}

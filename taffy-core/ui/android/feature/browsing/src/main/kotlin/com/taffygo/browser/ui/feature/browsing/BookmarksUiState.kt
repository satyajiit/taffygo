// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import android.graphics.Bitmap

/**
 * Screen SCR-202 — pages the person starred, grouped by folder.
 *
 * Empty is not unavailable, and a search that matches nothing is not empty.
 * Portable transfer is stateful, and a cancelled picker is not a failure.
 */
data class BookmarksUiState(
    val query: String = "",
    val availability: Availability = Availability.READY,
    val folders: List<BookmarkFolder> = emptyList(),
    val totalCount: Int = 0,
    val currentFolderId: BookmarkFolder.Id? = null,
    val editing: Bookmark? = null,
    val siteMarks: Map<String, Bitmap> = emptyMap(),
    val transferStatus: TransferStatus = TransferStatus.IDLE,
    val importResult: BookmarkTransferDocument.ImportResult? = null,
) {
    /** Whether the list has arrived, and whether it exists at all. */
    enum class Availability {
        LOADING,
        READY,
        UNAVAILABLE,
    }

    enum class TransferStatus {
        IDLE,
        CHOOSING_IMPORT,
        READING_IMPORT,
        IMPORTING,
        CHOOSING_EXPORT,
        WRITING_EXPORT,
        IMPORTED,
        EXPORTED,
        FAILED,
    }

    /** Whether there is nothing at all, as opposed to nothing matching. */
    val isEmpty: Boolean
        get() = availability == Availability.READY && totalCount == 0

    /** Bookmarks actually drawn after search and folder scope. */
    val visibleCount: Int
        get() = folders.sumOf { it.bookmarks.size }

    /** Whether a search or folder found nothing, which is a different sentence. */
    val hasNoMatches: Boolean
        get() = availability == Availability.READY && totalCount > 0 && visibleCount == 0

    val isLoading: Boolean
        get() = availability == Availability.LOADING

    val isUnavailable: Boolean
        get() = availability == Availability.UNAVAILABLE

    /** Whether tapping a row may open the page. */
    val canOpen: Boolean
        get() = availability == Availability.READY

    val transferBusy: Boolean
        get() = when (transferStatus) {
            TransferStatus.CHOOSING_IMPORT,
            TransferStatus.READING_IMPORT,
            TransferStatus.IMPORTING,
            TransferStatus.CHOOSING_EXPORT,
            TransferStatus.WRITING_EXPORT,
            -> true
            TransferStatus.IDLE,
            TransferStatus.IMPORTED,
            TransferStatus.EXPORTED,
            TransferStatus.FAILED,
            -> false
        }

    val canTransfer: Boolean
        get() = availability == Availability.READY && !transferBusy

    /** The folder the person drilled into, if any. */
    val currentFolder: BookmarkFolder?
        get() = currentFolderId?.let { id -> folders.firstOrNull { it.id == id } }
}

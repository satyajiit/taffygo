// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

/** Everything screen SCR-202 can be asked to do. */
sealed interface BookmarksIntent {

    /** Narrow the list. Empty query shows every starred page. */
    data class QueryChanged(val query: String) : BookmarksIntent

    /** Open the page this bookmark is about. */
    data class Open(val id: Bookmark.Id) : BookmarksIntent

    /** Open the editor sheet for one bookmark. */
    data class Edit(val id: Bookmark.Id) : BookmarksIntent

    /** Ask Android to choose one portable bookmark document. */
    data object RequestImport : BookmarksIntent

    /** Ask Android to choose one export destination. */
    data object RequestExport : BookmarksIntent

    /** The person left the import picker without choosing a document. */
    data object ImportPickerCancelled : BookmarksIntent

    /** The person left the export picker without choosing a destination. */
    data object ExportPickerCancelled : BookmarksIntent

    /** Android could not open the requested picker. */
    data object TransferFailed : BookmarksIntent

    /** Keep the edited name and folder. */
    data class SaveEdit(val title: String, val folderId: BookmarkFolder.Id) : BookmarksIntent

    /** Close the editor without writing. */
    data object DismissEdit : BookmarksIntent

    /** Remove one star, after the screen has confirmed. */
    data class Delete(val id: Bookmark.Id) : BookmarksIntent

    /** Show only this folder. */
    data class OpenFolder(val id: BookmarkFolder.Id) : BookmarksIntent

    /** Leave the folder, or leave this screen. */
    data object Dismiss : BookmarksIntent
}

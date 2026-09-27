// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.workspaces

/** Everything screen SCR-502 can be asked to do. */
sealed interface LibraryCollectionIntent {

    /** The person typed in the search field. */
    data class QueryChanged(val query: String) : LibraryCollectionIntent

    /** Open one kept item. */
    data class OpenItem(val itemId: String) : LibraryCollectionIntent

    /** Start only the exact source/work preview the person just approved. */
    data class ApproveRefresh(
        val preview: LibraryRepository.RefreshPreview,
    ) : LibraryCollectionIntent

    /** Ask the store for a file of this collection. */
    data object Export : LibraryCollectionIntent

    /** Ask the store to delete this collection for real. */
    data object RemoveCollection : LibraryCollectionIntent

    data class RenameCollection(
        val expectedRevision: ULong,
        val displayName: String,
    ) : LibraryCollectionIntent

    data class DeleteCollection(
        val expectedRevision: ULong,
        val confirmationToken: String,
    ) : LibraryCollectionIntent
}

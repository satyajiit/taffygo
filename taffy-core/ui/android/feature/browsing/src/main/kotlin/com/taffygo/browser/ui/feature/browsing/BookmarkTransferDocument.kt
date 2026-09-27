// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

/** A bounded, provider-neutral bookmark tree used only at the import/export seam. */
data class BookmarkTransferDocument(
    val bookmarks: List<Entry> = emptyList(),
    val folders: List<Folder> = emptyList(),
    val rejectedEntries: Int = 0,
) {
    data class Entry(val title: String, val address: String)

    data class Folder(
        val title: String,
        val bookmarks: List<Entry> = emptyList(),
        val folders: List<Folder> = emptyList(),
    )

    /** Exact outcome of merging one portable document into the live bookmark model. */
    data class ImportResult(
        val imported: Int,
        val duplicates: Int,
        val rejected: Int,
    ) {
        val changed: Boolean
            get() = imported > 0
    }

    val entryCount: Int
        get() {
            var count = bookmarks.size
            val pending = ArrayDeque<Folder>()
            folders.forEach(pending::addLast)
            while (pending.isNotEmpty()) {
                val folder = pending.removeLast()
                count += folder.bookmarks.size
                folder.folders.forEach(pending::addLast)
            }
            return count
        }
}

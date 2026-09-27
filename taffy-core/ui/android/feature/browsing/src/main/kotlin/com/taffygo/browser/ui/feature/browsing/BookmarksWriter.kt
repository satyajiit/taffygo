// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

/**
 * The write half of bookmarks, for the save-page sheet.
 *
 * History and Bookmarks screens own the reader. This port is the only
 * thing SCR-811 may call, so the two can meet later without this feature
 * depending on those screens.
 */
interface BookmarksWriter {

    /** Whether a page can actually be kept on this build. */
    val isAvailable: Boolean

    /**
     * Star the exact canonical HTTP(S) [address] into [folderId]. Empty
     * [folderId] is the default "All". Returns false when nothing was written.
     */
    suspend fun save(title: String, address: String, folderId: String): Boolean
}

/** The closed writer: Save stays disabled and nothing is stored. */
internal class EmptyBookmarksWriter : BookmarksWriter {
    override val isAvailable: Boolean = false

    override suspend fun save(title: String, address: String, folderId: String): Boolean = false
}

// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

/**
 * One page the person starred.
 *
 * Bookmarks are whole pages, not Library facts and not Memory. Private tabs
 * cannot be starred; a writer that is asked to save one must refuse before
 * it reaches this list.
 */
data class Bookmark(
    val id: Id,
    val title: String,
    val host: String,
    val folderId: BookmarkFolder.Id = BookmarkFolder.Id.ALL,
    /** Exact HTTP(S) address retained for open and portable export. */
    val address: String = host,
) {
    /** Opaque bookmark identifier. */
    @JvmInline
    value class Id(val value: String)
}

// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

/**
 * One folder of starred pages.
 *
 * [Id.ALL] is the default folder Save page writes into. There is no reading
 * list; a second pile of pages would teach the wrong word for Library.
 */
data class BookmarkFolder(
    val id: Id,
    val name: String,
    val bookmarks: List<Bookmark> = emptyList(),
) {
    /** Opaque folder identifier. */
    @JvmInline
    value class Id(val value: String) {
        companion object {
            /** The default folder, shown as "All". */
            val ALL: Id = Id("all")
        }
    }
}

// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

/**
 * What [BookmarksRepository] currently knows.
 *
 * Ready with no starred pages is empty, not unavailable. Unavailable means
 * there is no list to read, so the screen must not invent folders.
 */
sealed interface BookmarksSnapshot {
    /** The list has not arrived yet. */
    data object Loading : BookmarksSnapshot

    /** There is no bookmark list to read. */
    data object Unavailable : BookmarksSnapshot

    /** The folders as stored. An empty All folder is honest empty. */
    data class Ready(
        val folders: List<BookmarkFolder>,
        /** False when the bounded browser reader stopped before the model ended. */
        val complete: Boolean = true,
    ) : BookmarksSnapshot
}

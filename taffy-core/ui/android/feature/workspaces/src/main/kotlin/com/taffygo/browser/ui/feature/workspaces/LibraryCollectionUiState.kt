// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.workspaces

import com.taffygo.browser.ui.core.model.WorkspaceDeletionPreview

/**
 * Screen SCR-502 — the items in one collection.
 *
 * [missing] is a collection identifier that does not resolve, which is not the
 * same as a collection that exists and holds nothing.
 */
data class LibraryCollectionUiState(
    /** Whether the store has not answered yet. */
    val loading: Boolean = false,
    /** Whether the store is not connected. */
    val unavailable: Boolean = false,
    /** Whether the identifier resolved at all. */
    val missing: Boolean = false,
    /** The collection, when there is one. */
    val collectionId: String = "",
    /** The collection's name. */
    val name: String = "",
    val revision: ULong = 0uL,
    val goal: String = "",
    val deletionPreview: WorkspaceDeletionPreview? = null,
    /** Display sentence the port already wrote, or absent. */
    val freshness: String? = null,
    /** Conflict count the port supplied; zero means no badge. */
    val conflictCount: Int = 0,
    /** Exact content-free source/work preview, or absent when reopening is unsafe. */
    val refreshPreview: LibraryRepository.RefreshPreview? = null,
    /** Latest content-free comparison against the preserved original. */
    val refreshResult: LibraryRepository.RefreshResult? = null,
    /** Items that match the query. */
    val items: List<LibraryRepository.Item> = emptyList(),
    /** What the person is searching for. */
    val query: String = "",
    /** How many items the collection holds, matched or not. */
    val totalCount: Int = 0,
    /** Whether refresh, export, or remove can actually run. */
    val canMutate: Boolean = false,
    /** Whether workspace-backed collection management actions are live. */
    val canManage: Boolean = false,
) {
    /** The collection exists and holds nothing. */
    val isEmpty: Boolean
        get() = !loading && !unavailable && !missing && totalCount == 0

    /** A search found nothing, which is a different sentence. */
    val hasNoMatches: Boolean
        get() = !loading && !unavailable && !missing && totalCount > 0 && items.isEmpty()
}

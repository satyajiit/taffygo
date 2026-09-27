// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.workspaces

/**
 * Fixtures the Library tests share.
 *
 * They are built here rather than taken from a seed, so a change to a port
 * cannot quietly change what these tests assert. Nothing here is a kept fact
 * the product would show.
 */
internal fun librarySource(
    host: String = "docs.example.test",
    title: String = "Page at $host",
) = LibraryRepository.Source(host = host, title = title)

internal fun libraryItem(
    id: String = "item_0",
    collectionId: String = "col_0",
    title: String = "Warranty is two years",
    body: String = "The listing says the warranty is two years.",
    sources: List<LibraryRepository.Source> = listOf(librarySource()),
    capturedAt: String? = "12 May",
    freshness: String? = null,
    hasConflict: Boolean = false,
    conflictSummary: String? = null,
    related: List<LibraryRepository.RelatedItem> = emptyList(),
) = LibraryRepository.Item(
    id = id,
    collectionId = collectionId,
    title = title,
    body = body,
    sources = sources,
    capturedAt = capturedAt,
    freshness = freshness,
    hasConflict = hasConflict,
    conflictSummary = conflictSummary,
    related = related,
)

internal fun libraryCollection(
    id: String = "col_0",
    name: String = "Home project",
    freshness: String? = "Checked 3 weeks ago",
    conflictCount: Int = 0,
    canManage: Boolean = false,
    items: List<LibraryRepository.Item> = listOf(libraryItem(collectionId = id)),
) = LibraryRepository.Collection(
    id = id,
    name = name,
    freshness = freshness,
    conflictCount = conflictCount,
    canManage = canManage,
    items = items,
)

internal fun readyLibrary(vararg collections: LibraryRepository.Collection) =
    LibraryRepository.Snapshot.Ready(collections.toList())

internal fun readyLibrarySearch(
    query: String,
    itemIds: Set<String>,
    vararg collections: LibraryRepository.Collection,
) = LibraryRepository.Snapshot.Ready(
    collections = collections.toList(),
    search = LibraryRepository.Search(query, itemIds),
)

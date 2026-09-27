// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.workspaces

/** Fixed states for this feature's Library previews. */
object LibraryPreviewStates {

    private val conflictItem = LibraryRepository.Item(
        id = "item_conflict",
        collectionId = "col_home",
        title = "Warranty is two years",
        body = "The listing says the warranty is two years.",
        sources = listOf(
            LibraryRepository.Source("docs.example.test", "Listing"),
            LibraryRepository.Source("shop.example.test", "Spec sheet"),
        ),
        capturedAt = "12 May",
        hasConflict = true,
        conflictSummary = "Two years on one page, one year on the other.",
        related = listOf(
            LibraryRepository.RelatedItem("col_home", "item_plain", "Return window"),
        ),
    )

    private val plainItem = LibraryRepository.Item(
        id = "item_plain",
        collectionId = "col_home",
        title = "Return window is 14 days",
        body = "Returns are accepted within 14 days.",
        sources = listOf(LibraryRepository.Source("docs.example.test", "Listing")),
        capturedAt = "12 May",
    )

    private val home = LibraryRepository.Collection(
        id = "col_home",
        name = "Home project",
        freshness = "Checked 3 weeks ago",
        conflictCount = 1,
        items = listOf(conflictItem, plainItem),
    )

    private val travel = LibraryRepository.Collection(
        id = "col_travel",
        name = "Travel",
        freshness = null,
        conflictCount = 0,
        items = listOf(
            LibraryRepository.Item(
                id = "item_train",
                collectionId = "col_travel",
                title = "Train times",
                body = "The 09:12 leaves from platform 4.",
                sources = listOf(LibraryRepository.Source("rail.example.test")),
                capturedAt = "3 April",
            ),
        ),
    )

    private val ready = LibraryRepository.Snapshot.Ready(listOf(home, travel))

    /** Screen SCR-501 with two collections, one carrying a conflict. */
    val homePopulated: LibraryHomeUiState = projectLibraryHome(ready, "")

    /** Screen SCR-501 with nothing kept. */
    val homeEmpty: LibraryHomeUiState = projectLibraryHome(
        LibraryRepository.Snapshot.Ready(emptyList()),
        "",
    )

    /** Screen SCR-501 while the store is down. */
    val homeUnavailable: LibraryHomeUiState =
        projectLibraryHome(LibraryRepository.Snapshot.Unavailable, "")

    /** Screen SCR-501 while the store has not answered. */
    val homeLoading: LibraryHomeUiState =
        projectLibraryHome(LibraryRepository.Snapshot.Loading, "")

    /** Screen SCR-501 when a search found nothing. */
    val homeNoMatches: LibraryHomeUiState = projectLibraryHome(ready, "no such thing")

    /** Screen SCR-502 with items, including a conflict. */
    val collection: LibraryCollectionUiState = projectLibraryCollection(
        snapshot = ready,
        collectionId = "col_home",
        query = "",
        canMutate = false,
    )

    /** Screen SCR-502 with nothing in it. */
    val collectionEmpty: LibraryCollectionUiState = projectLibraryCollection(
        snapshot = LibraryRepository.Snapshot.Ready(
            listOf(home.copy(items = emptyList(), conflictCount = 0)),
        ),
        collectionId = "col_home",
        query = "",
        canMutate = false,
    )

    /** Screen SCR-503 with sources, a conflict, and a related item. */
    val item: LibraryItemUiState = projectLibraryItem(
        snapshot = ready,
        collectionId = "col_home",
        itemId = "item_conflict",
        canMutate = false,
    )

    /** Screen SCR-504 disconnected, with collections to choose. */
    val keepThis: KeepThisUiState = projectKeepThis(
        snapshot = ready,
        selectedCollectionId = "col_home",
        selectedKind = KeepThisKind.FACT,
        canKeep = false,
    )

    /** Screen SCR-504 with nothing to keep into. */
    val keepThisEmpty: KeepThisUiState = projectKeepThis(
        snapshot = LibraryRepository.Snapshot.Ready(emptyList()),
        selectedCollectionId = null,
        selectedKind = null,
        canKeep = false,
    )
}

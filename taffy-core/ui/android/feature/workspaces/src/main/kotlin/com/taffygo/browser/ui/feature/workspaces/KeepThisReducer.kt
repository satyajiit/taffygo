// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.workspaces

/**
 * What screen SCR-504 shows, and how a choice is recorded.
 *
 * Keep does not flip a success flag: Empty and Unavailable cannot add a fact,
 * so the projection never pretends one was kept.
 */
internal fun projectKeepThis(
    snapshot: LibraryRepository.Snapshot,
    selectedCollectionId: String?,
    selectedKind: KeepThisKind?,
    canKeep: Boolean,
): KeepThisUiState {
    val collections = when (snapshot) {
        is LibraryRepository.Snapshot.Ready -> snapshot.collections
        LibraryRepository.Snapshot.Loading,
        LibraryRepository.Snapshot.Unavailable,
        -> emptyList()
    }
    val stillSelected = selectedCollectionId
        ?.takeIf { id -> collections.any { it.id == id } }
    return KeepThisUiState(
        loading = snapshot is LibraryRepository.Snapshot.Loading,
        unavailable = snapshot is LibraryRepository.Snapshot.Unavailable,
        collections = collections,
        selectedCollectionId = stillSelected,
        selectedKind = selectedKind,
        canKeep = canKeep,
    )
}

/** Apply a choice. Keep and Dismiss leave the fields as they are. */
internal fun reduceKeepThis(
    state: KeepThisUiState,
    intent: KeepThisIntent,
): KeepThisUiState = when (intent) {
    is KeepThisIntent.SelectCollection ->
        if (state.collections.any { it.id == intent.collectionId }) {
            state.copy(selectedCollectionId = intent.collectionId)
        } else {
            state
        }
    is KeepThisIntent.SelectKind -> state.copy(selectedKind = intent.kind)
    KeepThisIntent.Keep, KeepThisIntent.Dismiss -> state
}

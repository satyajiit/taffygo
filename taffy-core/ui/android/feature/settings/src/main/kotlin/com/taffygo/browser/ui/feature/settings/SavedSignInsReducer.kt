// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

/** Screen SCR-413's pure metadata-only state transition. */
internal fun reduceSavedSignIns(
    state: SavedSignInsUiState,
    intent: SavedSignInsIntent,
): SavedSignInsUiState = when (intent) {
    is SavedSignInsIntent.QueryChanged -> state.copy(query = intent.query)
    is SavedSignInsIntent.Open -> state.copy(
        opened = state.records.firstOrNull { it.id == intent.id },
        confirmDelete = false,
    )
    SavedSignInsIntent.DismissDetail ->
        state.copy(opened = null, confirmDelete = false)
    SavedSignInsIntent.Delete -> state.copy(confirmDelete = true)
    SavedSignInsIntent.ConfirmDelete -> state.copy(
        opened = null,
        confirmDelete = false,
        records = state.records.filterNot { it.id == state.opened?.id },
    )
    SavedSignInsIntent.CancelDelete -> state.copy(confirmDelete = false)
}

internal fun projectSavedSignIns(
    snapshot: SavedSignInsRepository.Snapshot,
    query: String,
    openedId: String?,
    confirmDelete: Boolean,
): SavedSignInsUiState {
    val records = if (snapshot.availability == YouSurfaceAvailability.READY) {
        snapshot.records
    } else {
        emptyList()
    }
    val opened = openedId?.let { id -> records.firstOrNull { it.id == id } }
    return SavedSignInsUiState(
        availability = snapshot.availability,
        query = query,
        records = records,
        opened = opened,
        confirmDelete = confirmDelete && opened != null,
    )
}

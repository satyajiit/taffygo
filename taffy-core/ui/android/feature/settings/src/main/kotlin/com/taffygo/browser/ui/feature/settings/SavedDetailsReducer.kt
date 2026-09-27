// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

/** Editor and confirm state. Persistence stays in the view model. */
internal fun reduceSavedDetails(
    state: SavedDetailsUiState,
    intent: SavedDetailsIntent,
): SavedDetailsUiState = when (intent) {
    SavedDetailsIntent.Add ->
        if (state.availability != YouSurfaceAvailability.READY) {
            state
        } else {
            state.copy(editor = SavedDetailsUiState.Editor(), confirmDelete = false)
        }
    is SavedDetailsIntent.Edit -> {
        val person = state.people.firstOrNull { it.id == intent.id }
        state.copy(editor = person?.toEditor(), confirmDelete = false)
    }
    is SavedDetailsIntent.EditorChanged -> state.copy(editor = intent.editor)
    SavedDetailsIntent.Save,
    SavedDetailsIntent.DismissEditor,
    -> state.copy(editor = null, confirmDelete = false)
    SavedDetailsIntent.Delete ->
        if (state.editor?.id == null) state else state.copy(confirmDelete = true)
    SavedDetailsIntent.ConfirmDelete -> state.copy(editor = null, confirmDelete = false)
    SavedDetailsIntent.CancelDelete -> state.copy(confirmDelete = false)
}

internal fun projectSavedDetails(
    snapshot: SavedDetailsRepository.Snapshot,
    editor: SavedDetailsUiState.Editor?,
    confirmDelete: Boolean,
): SavedDetailsUiState = SavedDetailsUiState(
    availability = snapshot.availability,
    people = if (snapshot.availability == YouSurfaceAvailability.READY) {
        snapshot.people
    } else {
        emptyList()
    },
    editor = editor,
    confirmDelete = confirmDelete && editor != null,
)

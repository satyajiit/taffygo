// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

/** Search, editor, and confirm. Persistence stays in the view model. */
internal fun reduceMemory(state: MemoryUiState, intent: MemoryIntent): MemoryUiState = when (intent) {
    is MemoryIntent.QueryChanged -> state.copy(query = intent.query)
    is MemoryIntent.Open -> {
        val note = state.visibleNotes.youWrote.firstOrNull { it.id == intent.id }
            ?: state.visibleNotes.taffyNoticed.firstOrNull { it.id == intent.id }
        state.copy(
            editor = note?.let {
                MemoryUiState.Editor(
                    id = it.id,
                    text = it.statement,
                    whyNote = it,
                    expectedMemoryRevision = state.revision,
                    expectedRecordRevision = it.revision,
                )
            },
            confirmDelete = false,
            mutationFailure = null,
        )
    }
    MemoryIntent.Add ->
        if (state.availability != YouSurfaceAvailability.READY) {
            state
        } else {
            state.copy(
                editor = MemoryUiState.Editor(expectedMemoryRevision = state.revision),
                confirmDelete = false,
                mutationFailure = null,
            )
        }
    is MemoryIntent.ChangeText -> state.copy(
        editor = state.editor?.copy(text = intent.text),
        mutationFailure = null,
    )
    MemoryIntent.Save -> state
    MemoryIntent.DismissEditor -> state.copy(
        editor = null,
        confirmDelete = false,
        mutationFailure = null,
    )
    MemoryIntent.Delete ->
        if (state.editor?.id == null) {
            state
        } else {
            state.copy(confirmDelete = true, mutationFailure = null)
        }
    MemoryIntent.ConfirmDelete -> state
    MemoryIntent.CancelDelete -> state.copy(confirmDelete = false, mutationFailure = null)
}

internal fun projectMemory(
    snapshot: MemoryRepository.Snapshot,
    query: String,
    editor: MemoryUiState.Editor?,
    confirmDelete: Boolean,
): MemoryUiState = MemoryUiState(
    availability = snapshot.availability,
    revision = snapshot.revision,
    totalNoteCount = if (snapshot.availability == YouSurfaceAvailability.READY) {
        snapshot.notes.size
    } else {
        0
    },
    visibleNotes = if (snapshot.availability == YouSurfaceAvailability.READY) {
        projectVisibleMemoryNotes(snapshot.notes, snapshot.search, query)
    } else {
        MemoryUiState.VisibleNotes()
    },
    processScoped = snapshot.processScoped,
    query = query,
    editor = editor,
    confirmDelete = confirmDelete && editor != null,
)

/** One bounded classification, run only when the Memory revision or query changes. */
internal fun projectVisibleMemoryNotes(
    notes: List<MemoryRepository.Note>,
    search: MemoryRepository.Search?,
    query: String,
): MemoryUiState.VisibleNotes {
    val needle = query.trim()
    val matchingIds = search
        ?.takeIf { it.query == query }
        ?.memoryIds
        .orEmpty()
    val youWrote = ArrayList<MemoryRepository.Note>()
    val taffyNoticed = ArrayList<MemoryRepository.Note>()
    for (note in notes) {
        if (needle.isNotEmpty() && note.id !in matchingIds) continue
        when (note.source) {
            MemoryRepository.Source.YOU_WROTE -> youWrote += note
            MemoryRepository.Source.TAFFY_NOTICED -> taffyNoticed += note
        }
    }
    return MemoryUiState.VisibleNotes(youWrote = youWrote, taffyNoticed = taffyNoticed)
}

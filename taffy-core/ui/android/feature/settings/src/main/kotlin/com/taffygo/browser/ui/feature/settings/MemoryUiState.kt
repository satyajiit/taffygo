// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import com.taffygo.browser.ui.core.common.FailureReason

/** Screen SCR-505 — Memory list and editor sheet. */
data class MemoryUiState(
    val availability: YouSurfaceAvailability = YouSurfaceAvailability.READY,
    val revision: ULong = 0uL,
    val totalNoteCount: Int = 0,
    val visibleNotes: VisibleNotes = VisibleNotes(),
    val processScoped: Boolean = false,
    val query: String = "",
    val editor: Editor? = null,
    val confirmDelete: Boolean = false,
    val mutationInFlight: Boolean = false,
    val mutationFailure: MutationFailure? = null,
) {
    /** Search chrome only above eight rows. */
    val showSearch: Boolean get() = totalNoteCount > SEARCH_THRESHOLD

    data class VisibleNotes(
        val youWrote: List<MemoryRepository.Note> = emptyList(),
        val taffyNoticed: List<MemoryRepository.Note> = emptyList(),
    ) {
        val isEmpty: Boolean
            get() = youWrote.isEmpty() && taffyNoticed.isEmpty()
    }

    /** Add or edit one note. */
    data class Editor(
        val id: String? = null,
        val text: String = "",
        val whyNote: MemoryRepository.Note? = null,
        val expectedMemoryRevision: ULong = 0uL,
        val expectedRecordRevision: ULong = 0uL,
    )

    /** A content-free rejected write; the editor remains the source of truth. */
    data class MutationFailure(
        val operation: MutationOperation,
        val reason: FailureReason,
    )

    enum class MutationOperation { SAVE, DELETE }

    companion object {
        const val SEARCH_THRESHOLD: Int = 8
    }
}

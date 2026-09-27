// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

/** Everything screen SCR-505 can be asked to do. */
sealed interface MemoryIntent {

    /** The person typed in search. */
    data class QueryChanged(val query: String) : MemoryIntent

    /** Open [id] in the editor. */
    data class Open(val id: String) : MemoryIntent

    /** Open a blank editor. */
    data object Add : MemoryIntent

    /** Replace the draft text. */
    data class ChangeText(val text: String) : MemoryIntent

    /** Write the draft. */
    data object Save : MemoryIntent

    /** Close the editor. */
    data object DismissEditor : MemoryIntent

    /** Ask to delete the open note. */
    data object Delete : MemoryIntent

    /** Confirm delete. */
    data object ConfirmDelete : MemoryIntent

    /** Keep the note. */
    data object CancelDelete : MemoryIntent
}

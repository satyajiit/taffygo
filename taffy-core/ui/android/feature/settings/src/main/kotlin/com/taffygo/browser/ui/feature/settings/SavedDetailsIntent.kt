// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

/** Everything screen SCR-414 can be asked to do. */
sealed interface SavedDetailsIntent {

    /** Open a blank editor. */
    data object Add : SavedDetailsIntent

    /** Open [id] in the editor. */
    data class Edit(val id: String) : SavedDetailsIntent

    /** Replace the draft. */
    data class EditorChanged(val editor: SavedDetailsUiState.Editor) : SavedDetailsIntent

    /** Write the draft. */
    data object Save : SavedDetailsIntent

    /** Close the editor. */
    data object DismissEditor : SavedDetailsIntent

    /** Ask to delete the open person. */
    data object Delete : SavedDetailsIntent

    /** Confirm delete. */
    data object ConfirmDelete : SavedDetailsIntent

    /** Keep the person. */
    data object CancelDelete : SavedDetailsIntent
}

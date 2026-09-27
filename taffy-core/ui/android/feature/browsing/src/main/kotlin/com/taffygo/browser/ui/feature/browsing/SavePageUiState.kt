// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

/**
 * Save page (SCR-811): a sheet over SCR-101, never a destination.
 *
 * The list of bookmarks is SCR-202. This sheet only stars the page in front
 * of the person. A private tab refuses in words rather than writing; a build
 * without a writer says so and keeps Save disabled.
 */
data class SavePageUiState(
    /** The page title as the page gave it. */
    val title: String = "",
    /** The host, shown instead of a full URL. */
    val host: String = "",
    /** The exact safe HTTP(S) address that will be stored. */
    val canonicalUrl: String = "",
    /** Whether the tab forgets everything when it closes. */
    val isPrivate: Boolean = false,
    /**
     * Whether a writer exists that can actually keep the page.
     *
     * Separate from [isPrivate]: a private tab would still refuse even if a
     * writer were present.
     */
    val canSave: Boolean = false,
    /**
     * Whether the folder list is still arriving.
     *
     * The empty writer answers immediately, so this stays false on this
     * build. A later writer that has to load folders sets it while it does.
     */
    val loading: Boolean = false,
    /** The folder being saved into, empty meaning the default "All". */
    val selectedFolderId: String = "",
    /** The latest write attempt for this exact open sheet. */
    val saveStatus: SaveStatus = SaveStatus.IDLE,
) {
    /** Save is offered only for an exact public web page while no write is in flight. */
    val primaryEnabled: Boolean
        get() = canSave &&
            !isPrivate &&
            canonicalUrl.isNotBlank() &&
            !loading &&
            saveStatus != SaveStatus.SAVING

    /** The truthful states of the sheet's one mutation. Success closes the sheet. */
    enum class SaveStatus {
        IDLE,
        SAVING,
        FAILED,
    }
}

// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.workspaces

/**
 * Screen SCR-504 — choose a collection and what is kept.
 *
 * [canKeep] is false until a port can actually add something. The Keep
 * control stays off in that case, so the screen never claims a fact was kept.
 */
data class KeepThisUiState(
    /** Whether the store has not answered yet. */
    val loading: Boolean = false,
    /** Whether the store is not connected. */
    val unavailable: Boolean = false,
    /** Collections the person can choose. */
    val collections: List<LibraryRepository.Collection> = emptyList(),
    /** The collection chosen, when it still exists. */
    val selectedCollectionId: String? = null,
    /** The kind chosen. */
    val selectedKind: KeepThisKind? = null,
    /** Whether the port can actually keep anything. */
    val canKeep: Boolean = false,
) {
    /** Kinds Keep this can name, in the order they are drawn. */
    val kinds: List<KeepThisKind>
        get() = KeepThisKind.entries

    /** Keep is offered only when a collection, a kind, and a live port agree. */
    val keepEnabled: Boolean
        get() = canKeep && selectedCollectionId != null && selectedKind != null
}

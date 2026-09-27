// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.workspaces

/**
 * Screen SCR-503 — one kept item, its sources, and related items.
 *
 * [missing] is an identifier that does not resolve. [hasConflict] follows the
 * port; the badge is omitted when the port does not supply one.
 */
data class LibraryItemUiState(
    /** Whether the store has not answered yet. */
    val loading: Boolean = false,
    /** Whether the store is not connected. */
    val unavailable: Boolean = false,
    /** Whether the identifier resolved at all. */
    val missing: Boolean = false,
    /** The collection this item belongs to. */
    val collectionId: String = "",
    /** The item, when there is one. */
    val itemId: String = "",
    /** What was kept, in the person's words or the fact's. */
    val title: String = "",
    /** The kept text itself. */
    val body: String = "",
    /** Where this item came from. */
    val sources: List<LibraryRepository.Source> = emptyList(),
    /** Display capture time the port already wrote, or absent. */
    val capturedAt: String? = null,
    /** Display freshness the port already wrote, or absent. */
    val freshness: String? = null,
    /** Whether the port marked a disagreement. */
    val hasConflict: Boolean = false,
    /** Both claims, when the port has them. */
    val conflictSummary: String? = null,
    /** Other kept items the port named. */
    val related: List<LibraryRepository.RelatedItem> = emptyList(),
    /** Whether remove can actually run. */
    val canMutate: Boolean = false,
)

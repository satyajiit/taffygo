// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.workspaces

import com.taffygo.browser.ui.core.model.Workspace

/**
 * Screen SCR-304 — every workspace, with the state each one really reached.
 *
 * The search query is part of the state rather than of the screen, so the
 * filtering rule is a pure function a test can pin down.
 */
data class WorkspaceListUiState(
    /** Every workspace that matches the query. */
    val workspaces: List<Workspace> = emptyList(),
    /** What the user is searching for. */
    val query: String = "",
    /** How many workspaces exist in total, matched or not. */
    val totalCount: Int = 0,
    /**
     * True until the repository has published a list. An empty first frame is
     * this, not [isEmpty], so the list does not flash "no workspaces yet".
     */
    val loading: Boolean = false,
    /** True when the core retained workspaces but omitted them from this reduced snapshot. */
    val unavailable: Boolean = false,
    /**
     * The browser profile these workspaces are kept in, named only when the
     * device has more than one and there is something to tell apart. A
     * workspace never leaves the profile that made it (decision 0102).
     */
    val profileName: String? = null,
) {
    /** Whether there is nothing at all, as opposed to nothing matching. */
    val isEmpty: Boolean
        get() = !loading && !unavailable && totalCount == 0

    /** Whether a search found nothing, which is a different sentence. */
    val hasNoMatches: Boolean
        get() = !loading && !unavailable && totalCount > 0 && workspaces.isEmpty()
}

// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.workspaces

/**
 * Screen SCR-306 — the page one fact came from.
 *
 * Capture time and extraction method are shown because a fact is only as good
 * as when and how it was read.
 */
data class SourceViewerUiState(
    /** The page title as the page gave it. */
    val title: String = "",
    /** The host, shown instead of a full URL. */
    val host: String = "",
    /** When the page was read, in epoch milliseconds. */
    val readAtEpochMillis: Long = 0,
    /** How many facts came from this page. */
    val factCount: Int = 0,
    /** Whether the user removed it from scope. */
    val excluded: Boolean = false,
    /**
     * True until the repository has published this source. A first frame with
     * no source is this, not [missing].
     */
    val loading: Boolean = false,
    /** Whether the complete workspace projection is unavailable. */
    val unavailable: Boolean = false,
    /** Whether the identifier resolved at all. */
    val missing: Boolean = false,
)

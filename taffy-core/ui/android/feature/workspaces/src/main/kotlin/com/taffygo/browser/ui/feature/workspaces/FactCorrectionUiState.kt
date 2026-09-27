// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.workspaces

/**
 * Screen SCR-307 — the fact correction sheet.
 *
 * [pageValue] is kept beside [enteredValue] and never replaced by it. That is
 * the whole point of the sheet: what the page said stays recorded, and what the
 * user entered is recorded as theirs.
 */
data class FactCorrectionUiState(
    /** Which output field this fact fills. */
    val field: String = "",
    /** What the page said. */
    val pageValue: String = "",
    /** What the user is entering. */
    val enteredValue: String = "",
    /** How many other cells rest on this one and would be recomputed. */
    val downstreamCount: Int = 0,
    /**
     * True until the repository has published this fact. A first frame with
     * no fact is this, not [missing].
     */
    val loading: Boolean = false,
    /** Whether the complete workspace projection is unavailable. */
    val unavailable: Boolean = false,
    /** Whether the identifier resolved at all. */
    val missing: Boolean = false,
) {
    /** Whether there is a change to save. */
    val canSave: Boolean
        get() = !loading && !unavailable && !missing &&
            enteredValue.isNotBlank() && enteredValue != pageValue
}

// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

/** Screen SCR-409 — how to reach us, and what this is. */
data class HelpUiState(
    /**
     * True once a feedback draft found no email app on this phone, so the
     * screen names the address instead of looking as though something opened.
     */
    val emailUnavailable: Boolean = false,
)

// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

/** Screen SCR-405 — the Taffy section hub. */
data class TaffySettingsUiState(
    /**
     * Whether the composer offers a suggestion while a person types (decision
     * `docs/decisions/0097-a-composer-suggestion-is-spent-from-the-persons-own-key.md`).
     *
     * Read from the stored preferences rather than held here, so the switch
     * shows what is saved and never what was just pressed.
     */
    val composerSuggestions: Boolean = false,
)

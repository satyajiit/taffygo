// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.browser

import com.taffygo.browser.ui.core.model.Suggestion

/** One already-safe local page candidate returned by an address-bar suggestion adapter. */
data class AddressBarPageSuggestion(
    val id: String,
    val title: String,
    val address: String,
    val host: String,
    val source: Suggestion.Source,
)

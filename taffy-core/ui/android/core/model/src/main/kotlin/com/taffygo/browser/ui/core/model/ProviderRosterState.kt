// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.model

/**
 * The provider roster as the isolated core last published it (decision 0080).
 *
 * [ready] is a different fact from an empty [rows], for the reason
 * [TaffyPartsState] records: an empty list is what a core that has not spoken
 * yet looks like, and it is also what a catalog with nothing offered would
 * look like, so a screen waiting on the list could not tell "still coming"
 * from "never" without this.
 */
data class ProviderRosterState(
    /** Whether the core has published a snapshot this list came from. */
    val ready: Boolean = false,
    /** Every provider the merged catalog carries, in the core's order. */
    val rows: List<ProviderRosterRow> = emptyList(),
)

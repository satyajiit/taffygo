// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.providers

/**
 * One group of provider rows, with the count the header shows.
 *
 * A section with no rows is never built, so a header on screen SCR-404 always
 * stands over something. The count is [rows] rather than a stored number,
 * because a header saying three over a list of two is the kind of defect that
 * survives a review.
 */
data class ProviderHubSection(
    /** Which group these rows belong to. */
    val group: ProviderHubGroup,
    /** The rows, in the order the core published them. */
    val rows: List<ProviderHubRow>,
) {
    /** How many providers this group holds. */
    val count: Int get() = rows.size
}

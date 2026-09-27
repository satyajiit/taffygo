// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

/**
 * Find in page (SCR-106): chrome on SCR-101, never a destination.
 *
 * The overlay sits in the bottom chrome, above the action row. The page
 * surface is not moved, clipped, or transformed to make room for it. [query]
 * lives only while the overlay is open — closing empties it, and nothing
 * writes it to History or Memory, including on a private tab.
 */
data class FindInPageUiState(
    /** Whether the overlay is drawn over the page. */
    val open: Boolean = false,
    /** The phrase being looked for. Empty until the person types. */
    val query: String = "",
    /**
     * Which match is current, 1-based when [matchCount] is greater than
     * zero. Zero means there is no current match, including "0 of 0".
     */
    val activeIndex: Int = 0,
    /** How many matches the engine reported. Zero is a real count. */
    val matchCount: Int = 0,
    /**
     * Whether this build can actually search the page.
     *
     * The overlay still opens when this is false. Next and previous stay
     * disabled, the count stays `0 of 0`, and no highlights are invented.
     */
    val available: Boolean = false,
) {
    /** The count is shown only once there is a phrase to look for. */
    val showsCount: Boolean
        get() = query.isNotBlank()

    /** Next and previous need a connected engine and at least one match. */
    val canMove: Boolean
        get() = available && matchCount > 0
}

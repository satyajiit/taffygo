// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

/**
 * Screen SCR-102 — a clean start page.
 *
 * A greeting, the one box, and the sites the person keeps returning to.
 * Nothing on this screen is suggested by TaffyGo: [frequent] is the person's
 * own visit counts, kept on this device by `FrequentSitesRepository` and
 * projected here with whatever favicons the open tabs can lend.
 *
 * Tabs arrive as a `StateFlow` that always has a value, so this screen is
 * never waiting on the tab list. It does wait on the Python library: the
 * start page is not drawn until that library is installed.
 */
data class NewTabUiState(
    /** The sites the person visits most, ranked by their own counts. */
    val frequent: List<FrequentTile> = emptyList(),
    /**
     * How many tabs the badge on this screen's own action row counts.
     *
     * Both halves are here rather than one total, and they come from the same
     * list `projectBrowserMain` counts, because the badge SCR-102 draws is the
     * badge SCR-101 draws: same glyph, same corner, and accent exactly while
     * Taffy owns a tab. Two screens deriving one number two ways is how the
     * number starts to differ between them.
     */
    val userTabCount: Int = 0,
    val taffyTabCount: Int = 0,
    /**
     * Whether the start page may be drawn, and how far the file it waits on is.
     *
     * Closed by default. The greeting, the grid and the address box are the
     * product looking ready, and they stay hidden until the library is
     * installed.
     */
    val startPageGate: StartPageGate = StartPageGate(),
) {
    /** Whether there is anything to show yet. */
    val isEmpty: Boolean
        get() = frequent.isEmpty()
}

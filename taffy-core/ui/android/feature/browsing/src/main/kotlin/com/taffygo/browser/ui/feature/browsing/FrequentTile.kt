// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import android.graphics.Bitmap

/**
 * One tile of the start page's frequent-sites grid.
 *
 * A tile rather than the `FrequentSite` it came from, because the favicon is
 * not the store's to know: the store persists hosts and counts, and every
 * favicon this build has is the engine's — keyed by open tab, or by host in
 * the profile's own favicon database. Projecting them together once, in the
 * reducer, keeps the grid from asking the browser anything while it draws —
 * the same reason SCR-104 draws [TabCard]s.
 */
data class FrequentTile(
    /** The host the tile opens. */
    val host: String,
    /** The page title, spoken with the host; falls back to the host. */
    val title: String,
    /** The site's mark, from an open tab or the profile's favicon store. */
    val favicon: Bitmap? = null,
)

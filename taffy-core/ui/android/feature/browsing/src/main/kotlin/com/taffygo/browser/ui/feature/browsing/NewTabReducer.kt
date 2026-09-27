// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import android.graphics.Bitmap
import com.taffygo.browser.ui.core.browser.FrequentSite
import com.taffygo.browser.ui.core.browser.TabArtwork
import com.taffygo.browser.ui.core.model.Tab
import com.taffygo.browser.ui.core.model.TabId

/**
 * What screen SCR-102 shows: the person's own most-visited sites, projected
 * into tiles — the start page is where the user starts, not a view of what
 * Taffy is doing.
 *
 * The ranking is the store's ([FrequentSite] arrives already ordered); this
 * projection only bounds it to what the grid draws and lends each tile the
 * engine's mark for its host — an open tab's favicon, or the profile's own
 * favicon store's. Private tabs never reach the store at all —
 * `FrequentSitesTracker` refuses them at recording time — so there is
 * nothing here to filter.
 */
internal fun projectNewTab(
    tabs: List<Tab>,
    startPageGate: StartPageGate = StartPageGate(),
    sites: List<FrequentSite> = emptyList(),
    artwork: Map<TabId, TabArtwork> = emptyMap(),
    siteMarks: Map<String, Bitmap> = emptyMap(),
): NewTabUiState = NewTabUiState(
    frequent = frequentTilesFrom(sites, tabs, artwork, siteMarks),
    // Counted over every tab, unlike `frequent` above. The grid is the places
    // the person returns to; the badge answers "how many tabs are open", and
    // a private tab or a tab on nothing is still an open tab.
    userTabCount = tabs.count { !it.isTaffyTab },
    taffyTabCount = tabs.count { it.isTaffyTab },
    startPageGate = startPageGate,
)

/**
 * The grid's tiles: the top of the ranking, each with the best mark this
 * build can honestly draw for it.
 *
 * Shared by SCR-101's empty tab and SCR-102, so the two surfaces cannot
 * disagree about which sites the person visits most. A tile's mark has two
 * local sources, tried in order: an open, non-private tab on the same host —
 * the engine's in-memory favicon, the freshest copy there is — and then the
 * profile's own favicon store ([siteMarks]), which remembers the mark after
 * the tab closes and across restarts. Only when both are empty does the tile
 * fall back to the site's initial: a store that has never seen the site, or
 * has not answered yet. A private tab's artwork is never borrowed, even for
 * a host the ranking already names: the mark was fetched in a tab that
 * promised to leave no trace.
 */
internal fun frequentTilesFrom(
    sites: List<FrequentSite>,
    tabs: List<Tab>,
    artwork: Map<TabId, TabArtwork>,
    siteMarks: Map<String, Bitmap> = emptyMap(),
): List<FrequentTile> {
    val faviconByHost = mutableMapOf<String, Bitmap>()
    tabs.forEach { tab ->
        if (tab.isPrivate || tab.host.isBlank()) return@forEach
        val favicon = artwork[tab.id]?.favicon ?: return@forEach
        faviconByHost.putIfAbsent(tab.host, favicon)
    }
    return sites.take(MAX_START_PAGE_TILES).map { site ->
        FrequentTile(
            host = site.host,
            title = site.title.ifBlank { site.host },
            favicon = faviconByHost[site.host] ?: siteMarks[site.host],
        )
    }
}

/** One row of four. A second row pushes the grid past the fold it sits above. */
internal const val MAX_START_PAGE_TILES = 4

// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag

/**
 * What screen SCR-101 draws when the tab in front of the person has no page.
 *
 * This is the start page, not a "nothing open" card. Opening a tab from the
 * switcher used to land here on a sentence that read as an error, while the
 * same moment reached through SCR-102 said "New tab". The body is therefore
 * [StartPageBody] — the greeting, the centred box and the person's sites,
 * sitting transparent on the backdrop the screen paints edge to edge
 * ([BrowserMainContent] at the glass, and the engine cover in
 * [BrowserPageArea] over the blank document). No top bar is drawn while this
 * is up
 * (`hasTopBar` reads the same `content` this branch was chosen by),
 * so the centred box is the one address entry on the screen; the action row
 * stays, because tabs and the browser's options are not part of the page.
 *
 * The page host stays composed underneath, covered, for the reason
 * [LivePageArea] states: destroying it would destroy the surface the next
 * page needs.
 */
@Composable
internal fun BrowserStartArea(
    frequent: List<FrequentTile>,
    composer: StartPageComposerSlot,
    onOpenSite: (String) -> Unit,
    modifier: Modifier = Modifier,
    showsFrequentSites: Boolean = true,
) {
    Box(
        modifier = modifier
            .fillMaxSize()
            .testTag(START_TEST_TAG),
    ) {
        StartPageBody(
            frequent = frequent,
            composer = composer,
            onOpenSite = onOpenSite,
            showsFrequentSites = showsFrequentSites,
        )
    }
}

/** The tag screen SCR-101's semantics tests name for the empty tab. */
const val START_TEST_TAG: String = "browser_start"

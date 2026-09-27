// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.ui.TaffyBrandLockup
import com.taffygo.browser.ui.core.ui.TaffyStartSceneImage

/**
 * The start-page body: the ground, a greeting, the one box, and the sites the
 * person keeps returning to.
 *
 * Shared by screen SCR-102 and by an empty tab on screen SCR-101, so the two
 * surfaces cannot say different things about the same moment. It is built
 * around its middle: the box sits at the vertical centre of whatever space the
 * caller gives it, the greeting floats above, and the frequent-sites grid sits
 * below.
 *
 * **[StartPageComposerSlot.typing] is the one thing that rearranges it.** The
 * box is typed into where it stands (decision 0131), so the moment there are
 * words in it the welcome goes: the lockup, the greeting and the tiles leave,
 * and the column goes top-aligned to give the reading and the suggestions the
 * room they were using. A person who has started typing has been welcomed, and
 * keeping the welcome would push the first suggestion behind the keyboard —
 * which is the whole fault this design removed. Empty the box and everything
 * comes back where it was.
 *
 * **A caret is not words.** Tapping the box rearranges nothing: the keyboard
 * rises and the page stays the page. A page that emptied itself the moment it
 * was touched would be the full-screen change decision 0131 removed, wearing
 * this layout instead of its own.
 *
 * The ground is the **host's**, not this body's: [StartPageBackdrop] is
 * edge-to-edge weather, and this body is laid out inside the window insets,
 * so a backdrop drawn here would start below the status bar and the strip
 * above it would sit on a different colour. Each of the two hosts paints the
 * backdrop across its whole window and lets this body sit transparent on it.
 *
 * It owns its space rather than joining the caller's column, because centring
 * is a property of the whole area and a scrolling parent has no centre. The
 * column inside scrolls only when it must — 200% text on a short display —
 * and is exactly the viewport otherwise, which is what `heightIn(min =
 * viewport)` under `verticalScroll` says.
 *
 * There is still no feed, no topic rows, and nothing ranked by anyone but the
 * person: the grid is their own visit counts, kept on this device — one row of
 * it, because a second row pushes the page past the fold it sits above. A
 * private tab's caller passes `showsFrequentSites = false`, because those
 * counts belong to the regular profile (decision 0255).
 *
 * The plate above the wordmark is [TaffyStartSceneImage]: a first-party
 * painting picked by the device's own clock, delivered through the asset plane
 * and read off this disk. Nothing on this page reaches the network, which is
 * the rule decision 0050 states and this keeps.
 */
@Composable
internal fun StartPageBody(
    frequent: List<FrequentTile>,
    composer: StartPageComposerSlot,
    onOpenSite: (String) -> Unit,
    modifier: Modifier = Modifier,
    showsFrequentSites: Boolean = true,
) {
    BoxWithConstraints(modifier = modifier.fillMaxSize()) {
        // Bounded whenever the caller keeps this body out of a scrolling
        // parent, which both screens do; unbounded would make the minimum
        // meaningless, so it falls back to content height.
        val viewport = if (maxHeight != Dp.Infinity) maxHeight else 0.dp
        val typing = composer.typing
        val showArtwork = maxWidth >= 360.dp && LocalDensity.current.fontScale < 1.5f
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .verticalScroll(rememberScrollState())
                .heightIn(min = viewport)
                .padding(
                    horizontal = TaffyTheme.spacing.screenMargin,
                    vertical = TaffyTheme.spacing.snug,
                ),
            horizontalAlignment = Alignment.CenterHorizontally,
            verticalArrangement = Arrangement.spacedBy(
                TaffyTheme.spacing.snug,
                if (typing) Alignment.Top else Alignment.CenterVertically,
            ),
        ) {
            if (!typing) {
                if (showArtwork) {
                    TaffyStartSceneImage(
                        width = if (viewport < 560.dp) 120.dp else 168.dp,
                        // The plate carries one more step than the column's own
                        // rhythm, so it stands off the wordmark instead of
                        // sitting on it. Only this gap grows: the rest of the
                        // column is right at the shared spacing.
                        modifier = Modifier.padding(bottom = TaffyTheme.spacing.snug),
                    )
                }
                TaffyBrandLockup(height = LockupHeight, modifier = Modifier.testTag(START_BRAND_TEST_TAG))
                StartPageGreeting()
            }
            composer.content()
            if (!typing && showsFrequentSites) {
                StartPageFrequentSites(frequent = frequent, onOpenSite = onOpenSite)
            }
        }
    }
}

/**
 * The box the two start surfaces centre.
 *
 * The tag is unchanged from when this was a closed box that opened SCR-103, and
 * deliberately: it names the same control in the same place, and a semantics
 * test that finds the box by tag should keep finding it. What it does when
 * pressed is what changed.
 */
const val NEW_TAB_ADDRESS_TEST_TAG: String = "new_tab_address"
const val START_BRAND_TEST_TAG: String = "start_brand"

// The lockup is the handoff's first-run mark a step down.
private val LockupHeight = 32.dp

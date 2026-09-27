// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.windowInsetsPadding
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.drawBehind
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.stateDescription
import androidx.compose.ui.unit.dp
import com.taffygo.browser.ui.core.designsystem.TaffyBorders
import com.taffygo.browser.ui.core.designsystem.TaffyEdges
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.ui.TaffyAddressPill
import com.taffygo.browser.ui.core.ui.TaffyIcon
import com.taffygo.browser.ui.core.ui.taffyChromeWidth
import com.taffygo.browser.ui.core.ui.taffyString

/**
 * The top bar over a page: the address pill, and the one overflow control.
 *
 * Screen SCR-101's chrome is split across the two edges of the window
 * (decision 0119, which replaced the bottom-anchored rule the earlier handoff
 * carried). Where you are and the menu of everything else are at the top,
 * outside the thumb's easy reach but where every other browser puts them;
 * moving between pages — Back, Forward, Tabs — and the Assistant pill stay at
 * the bottom, where the thumb is. There is exactly one overflow control and it
 * is this one: the action row gave its far slot up rather than growing a
 * second copy, because two menus that open the same tiles drift apart the
 * moment one of them gains a row.
 *
 * ## It overlays the page, and never shortens it
 *
 * This is a top-aligned child of [BrowserPageColumn]'s box, a sibling of the
 * page surface, which fills that box whether or not this bar is drawn. It is
 * never a row of a column the page is measured against. That is the whole
 * reason it can hide on a scroll for nothing: leaving the composition changes
 * no constraint the page was given, so the compositor is not asked to relayout
 * the document, and the web engine never sees a viewport change. A `Column`
 * holding both would turn every scroll gesture into a reflow of the page being
 * scrolled.
 *
 * The page surface being pulled back off the window's own edges does not touch
 * that property, and the two are easy to confuse. This bar's height is not
 * part of the amount the page is pulled back by; only the window's shape is,
 * and a window does not change shape because somebody scrolled.
 *
 * It is the only piece of chrome that hides. The action row at the other end
 * of the window stays put, so the page is measured above that one and drawn
 * under this one — see [BrowserPageArea] for why the two ends differ.
 *
 * ## The status-bar inset is taken here, and again by the page
 *
 * [windowInsetsPadding] with [TaffyEdges.top] is applied **inside** the
 * background, so the bar's own surface reaches up behind the status bar while
 * its words and controls sit below it. The page's surface takes the same edge
 * for itself — see [BrowserPageArea] — and that is not the inset being spent
 * twice: consumption runs down a subtree, and this bar and the page are
 * siblings in the same box, each pulling its own content back off the same
 * strip. What is behind that strip is therefore this bar's ground while the
 * bar is up, and the page's own background colour once it has gone, which is
 * what the screen catalog's SCR-101 row asks for. The box above still takes
 * the inset in the states this bar is absent from — see [hasTopBar].
 */
@Composable
internal fun BrowserTopBar(
    state: BrowserMainUiState,
    onIntent: (BrowserMainIntent) -> Unit,
    modifier: Modifier = Modifier,
) {
    val chromeOnPage = state.content == BrowserContent.PAGE
    val chromeEdge = TaffyTheme.colors.outline
    Box(
        modifier = modifier
            .fillMaxWidth()
            .then(
                if (chromeOnPage) {
                    Modifier
                        .background(TaffyTheme.colors.surface)
                        .drawBehind {
                            val rule = TaffyBorders.standard.toPx()
                            drawRect(
                                color = chromeEdge,
                                topLeft = Offset(0f, size.height - rule),
                                size = Size(size.width, rule),
                            )
                        }
                } else {
                    Modifier
                },
            )
            .windowInsetsPadding(TaffyEdges.top)
            .testTag(TOP_BAR_TEST_TAG),
        // [taffyChromeWidth] caps the row on a tablet, and the bottom column
        // centres what it caps. A bar that left-aligned its own cap would put
        // the pill hard against the pane's edge while the action row under the
        // same page sat centred. On a phone the cap is unspecified, the row
        // fills the width, and this alignment does nothing at all.
        contentAlignment = Alignment.TopCenter,
    ) {
        Row(
            modifier = Modifier
                .taffyChromeWidth()
                .padding(
                    start = TaffyTheme.spacing.screenMargin,
                    top = TaffyTheme.spacing.step,
                    end = TaffyTheme.spacing.screenMargin,
                    bottom = TaffyTheme.spacing.step,
                ),
            horizontalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.step),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            AddressPill(
                state = state,
                onIntent = onIntent,
                modifier = Modifier.weight(1f),
            )
            Box {
                BrowserChromeButton(
                    icon = TaffyIcon.DotsThreeVertical,
                    contentDescription = taffyString(R.string.taffy_browser_page_actions),
                    onClick = { onIntent(BrowserMainIntent.OpenMore) },
                    testTag = MORE_TEST_TAG,
                    targetSize = TopBarTarget,
                )
                if (state.moreOpen) {
                    BrowserMenu(state = state, onIntent = onIntent)
                }
            }
        }
    }
}

/**
 * The overflow button's box, which is the address pill's height and not the
 * action row's.
 *
 * `BrowserChromeButton` defaults to the action row's 56 dp target, and taking
 * that default here made this bar twelve density-independent pixels taller
 * than the pill beside it — the tallest thing in the row decides, and it was
 * an invisible box rather than anything anybody could see. That is twelve
 * pixels of the page's own height spent on nothing, at the top of the window,
 * for the life of every session, and it was reported from a phone as the gap
 * between the status bar and the page being too large.
 *
 * The pill is the target this bar is built around and it is 44 dp, so the
 * control beside it is too. That is under the 48 dp the spacing grid calls a
 * minimum touch target, and deliberately: the pill is already a 44 dp target
 * and a row whose two controls disagree about their own height is a row that
 * is taller than either of them.
 */
private val TopBarTarget = 44.dp

/**
 * Whether the top bar belongs over this content at all.
 *
 * The same gate the address strip carried when it was the bottom column's
 * last-but-one row: a tab that has been nowhere, and one still waiting for the
 * page tools, draw the start page — which centres its own address box in its
 * body — and a second identical control above it would be two controls asking
 * the same question. Those two states are unchanged by the split: no top bar,
 * and the box above keeps their status-bar inset.
 *
 * This is content alone, deliberately. It decides where the status-bar inset
 * is consumed, and an inset that moved every time a scroll hid the bar would
 * resize the page area on every scroll — the one thing the overlay exists to
 * avoid. [showsTopBar] adds the scroll signal on top of it.
 */
internal fun BrowserMainUiState.hasTopBar(): Boolean =
    content != BrowserContent.PREPARING && content != BrowserContent.START

/** Whether the top bar is on screen now: [hasTopBar], and not scrolled away. */
internal fun BrowserMainUiState.showsTopBar(): Boolean = hasTopBar() && topBarVisible

@Composable
private fun AddressPill(
    state: BrowserMainUiState,
    onIntent: (BrowserMainIntent) -> Unit,
    modifier: Modifier = Modifier,
) {
    val description = when {
        state.isPrivate && state.host.isEmpty() ->
            taffyString(R.string.taffy_browser_address_description_blank_private)
        state.isPrivate ->
            taffyString(R.string.taffy_browser_address_description_private, state.host)
        state.host.isEmpty() -> taffyString(R.string.taffy_browser_address_description_blank)
        else -> taffyString(R.string.taffy_browser_address_description, state.host)
    }
    val loading = taffyString(R.string.taffy_browser_loading)
    TaffyAddressPill(
        url = state.host.ifEmpty { taffyString(R.string.taffy_browser_address_hint) },
        blockedCount = state.blockedRequestCount,
        isLoading = state.isLoading,
        isSecure = state.isSecure,
        favicon = state.favicon,
        onReload = { onIntent(BrowserMainIntent.Reload) },
        onStopLoading = { onIntent(BrowserMainIntent.StopLoading) },
        onBlockedBadge = { onIntent(BrowserMainIntent.OpenSiteFiltering) },
        modifier = modifier
            .clip(TaffyTheme.shapes.pill)
            .clickable(onClick = { onIntent(BrowserMainIntent.FocusAddressBar) })
            .testTag(ADDRESS_BAR_TEST_TAG)
            .semantics {
                contentDescription = description
                if (state.isLoading) stateDescription = loading
            },
    )
}

/** The top bar itself, which screen SCR-101's semantics tests name. */
const val TOP_BAR_TEST_TAG: String = "browser_top_bar"

/** The address bar, which screen SCR-101's semantics tests name. */
const val ADDRESS_BAR_TEST_TAG: String = "browser_address_bar"

/** The top bar's overflow control, at the far end of the address pill. */
const val MORE_TEST_TAG: String = "browser_more"

/** The anchored menu the overflow control opens, downward from that control. */
const val MORE_MENU_TEST_TAG: String = "browser_more_menu"

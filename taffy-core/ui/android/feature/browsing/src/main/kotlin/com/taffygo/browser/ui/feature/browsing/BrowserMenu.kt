// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.material3.Icon
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.disabled
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.IntOffset
import androidx.compose.ui.unit.dp
import androidx.compose.ui.window.Popup
import androidx.compose.ui.window.PopupProperties
import com.taffygo.browser.ui.core.designsystem.TaffyBorders
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.ui.TaffyGlyphFrame
import com.taffygo.browser.ui.core.ui.TaffyIcon
import com.taffygo.browser.ui.core.ui.taffyString

/**
 * The destinations no slot of the chrome has room for, in a grid under the
 * control that opened it.
 *
 * Three labelled groups (This site, Your pages, More). Tiles are 48 dp orbs
 * with a visible word underneath. Amber is never used: every tile here is a
 * manual control, and every tile here is a product surface. A diagnostic
 * drawn after More once was not, and a person reached it (decision 0129).
 *
 * **It drops down, because its control is now at the top of the window.** The
 * position is relative to the anchoring box — the overflow control itself — so
 * this is one alignment and one offset rather than a measurement: `TopEnd`
 * puts the sheet's top edge on the control's, and dropping it by the control's
 * own height plus one gap clears it. It opened *upward* by exactly the mirror
 * of that arithmetic while the control lived in the bottom action row, and
 * leaving that alone would have hung the whole grid off the top of the screen.
 */
@Composable
internal fun BrowserMenu(
    state: BrowserMainUiState,
    onIntent: (BrowserMainIntent) -> Unit,
) {
    BrowserMenuPopup(
        groups = browserMenuGroups(state, onIntent),
        onDismiss = { onIntent(BrowserMainIntent.DismissMore) },
    )
}

/**
 * The grid itself, anchored to whatever control opened it.
 *
 * [alignment] and [offset] are the only thing that differs between the two
 * controls that open this menu, and they differ by a mirror: the overflow at the
 * top of a page drops the sheet below itself, and the plus in the middle of the
 * start page raises it above, because a menu opened downward from the centre of
 * the window opens into the keyboard. Everything inside is the same grid, from
 * the same tiles, so the two cannot drift.
 */
@Composable
internal fun BrowserMenuPopup(
    groups: List<BrowserMenuGroup>,
    onDismiss: () -> Unit,
    alignment: Alignment = Alignment.TopEnd,
    rises: Boolean = false,
) {
    val step = with(LocalDensity.current) {
        (ActionTarget + TaffyTheme.spacing.tight).roundToPx()
    }
    Popup(
        alignment = alignment,
        offset = IntOffset(0, if (rises) -step else step),
        onDismissRequest = onDismiss,
        properties = PopupProperties(focusable = true),
    ) {
        Column(
            modifier = Modifier
                .clip(TaffyTheme.shapes.card)
                .background(TaffyTheme.colors.surfaceSheet)
                .border(
                    TaffyBorders.standard,
                    TaffyTheme.colors.outline,
                    TaffyTheme.shapes.card,
                )
                .padding(TaffyTheme.spacing.snug)
                .testTag(MORE_MENU_TEST_TAG),
            verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.tight),
        ) {
            groups.forEach { group ->
                val heading = group.heading
                val headingTag = group.headingTestTag
                if (heading != null && headingTag != null) {
                    MenuSectionLabel(text = heading, testTag = headingTag)
                }
                MenuTileRows(tiles = group.tiles)
            }
        }
    }
}

@Composable
private fun browserMenuGroups(
    state: BrowserMainUiState,
    onIntent: (BrowserMainIntent) -> Unit,
): List<BrowserMenuGroup> {
    val onPage = state.content == BrowserContent.PAGE ||
        state.content == BrowserContent.FAILED
    val groups = mutableListOf<BrowserMenuGroup>()
    if (onPage) {
        groups += BrowserMenuGroup(
            heading = taffyString(R.string.taffy_browser_menu_site),
            headingTestTag = MENU_SITE_HEADING_TEST_TAG,
            tiles = listOf(
                BrowserMenuTile(
                    icon = if (state.isLoading) TaffyIcon.StopCircle else TaffyIcon.ArrowClockwise,
                    label = taffyString(
                        if (state.isLoading) {
                            R.string.taffy_browser_menu_stop
                        } else {
                            R.string.taffy_browser_menu_reload
                        },
                    ),
                    onSelect = {
                        onIntent(
                            if (state.isLoading) {
                                BrowserMainIntent.StopLoading
                            } else {
                                BrowserMainIntent.Reload
                            },
                        )
                    },
                    testTag = if (state.isLoading) STOP_LOADING_TEST_TAG else RELOAD_TEST_TAG,
                ),
                BrowserMenuTile(
                    icon = TaffyIcon.MagnifyingGlass,
                    label = taffyString(R.string.taffy_browser_menu_find),
                    onSelect = { onIntent(BrowserMainIntent.OpenFindInPage) },
                    testTag = FIND_TEST_TAG,
                ),
                BrowserMenuTile(
                    icon = TaffyIcon.ShareNetwork,
                    label = taffyString(R.string.taffy_browser_menu_share),
                    onSelect = { onIntent(BrowserMainIntent.SharePage) },
                    testTag = SHARE_TEST_TAG,
                    enabled = state.canonicalUrl.isNotBlank(),
                ),
                BrowserMenuTile(
                    icon = TaffyIcon.Star,
                    label = taffyString(R.string.taffy_browser_menu_save_page),
                    onSelect = { onIntent(BrowserMainIntent.OpenSavePage) },
                    testTag = SAVE_PAGE_MENU_TEST_TAG,
                ),
                BrowserMenuTile(
                    icon = TaffyIcon.ShieldCheck,
                    label = taffyString(R.string.taffy_browser_menu_ads),
                    onSelect = { onIntent(BrowserMainIntent.OpenSiteFiltering) },
                    testTag = ADS_TEST_TAG,
                ),
            ) + if (!state.isPrivate && state.content == BrowserContent.PAGE) listOf(
                BrowserMenuTile(
                    icon = TaffyIcon.ClockCounterClockwise,
                    label = taffyString(R.string.taffy_page_flows_title),
                    onSelect = { onIntent(BrowserMainIntent.OpenSavedFlows) },
                    testTag = SAVED_FLOWS_MENU_TEST_TAG,
                ),
            ) else emptyList(),
        )
    }
    groups += BrowserMenuGroup(
        heading = taffyString(R.string.taffy_browser_menu_pages),
        headingTestTag = MENU_PAGES_HEADING_TEST_TAG,
        tiles = listOf(
            BrowserMenuTile(
                icon = TaffyIcon.ClockCounterClockwise,
                label = taffyString(R.string.taffy_browser_menu_history),
                onSelect = { onIntent(BrowserMainIntent.OpenHistory) },
                testTag = HISTORY_TEST_TAG,
            ),
            BrowserMenuTile(
                icon = TaffyIcon.BookmarkSimple,
                label = taffyString(R.string.taffy_browser_menu_bookmarks),
                onSelect = { onIntent(BrowserMainIntent.OpenBookmarks) },
                testTag = BOOKMARKS_TEST_TAG,
            ),
            BrowserMenuTile(
                icon = TaffyIcon.DownloadSimple,
                label = taffyString(R.string.taffy_browser_downloads),
                onSelect = { onIntent(BrowserMainIntent.OpenDownloads) },
                testTag = DOWNLOADS_TEST_TAG,
            ),
            BrowserMenuTile(
                icon = TaffyIcon.Books,
                label = taffyString(R.string.taffy_browser_menu_library),
                onSelect = { onIntent(BrowserMainIntent.OpenLibrary) },
                testTag = LIBRARY_TEST_TAG,
            ),
        ),
    )
    groups += BrowserMenuGroup(
        heading = taffyString(R.string.taffy_browser_more_title),
        headingTestTag = MENU_MORE_HEADING_TEST_TAG,
        tiles = listOf(
            BrowserMenuTile(
                icon = TaffyIcon.SquaresFour,
                label = taffyString(R.string.taffy_new_tab_workspaces),
                onSelect = { onIntent(BrowserMainIntent.OpenWorkspaces) },
                testTag = WORKSPACES_TEST_TAG,
            ),
            BrowserMenuTile(
                icon = TaffyIcon.UserCircle,
                label = taffyString(R.string.taffy_browser_menu_you),
                onSelect = { onIntent(BrowserMainIntent.OpenYou) },
                testTag = YOU_TEST_TAG,
            ),
            BrowserMenuTile(
                icon = TaffyIcon.Gear,
                label = taffyString(R.string.taffy_browser_settings),
                onSelect = { onIntent(BrowserMainIntent.OpenSettings) },
                testTag = SETTINGS_TEST_TAG,
            ),
        ),
    )
    return groups
}

@Composable
private fun MenuSectionLabel(text: String, testTag: String) {
    Text(
        text = text,
        style = TaffyTheme.typography.label,
        color = TaffyTheme.colors.textSecondary,
        modifier = Modifier
            .padding(horizontal = TaffyTheme.spacing.step)
            .testTag(testTag)
            .semantics { heading() },
    )
}

@Composable
private fun MenuTileRows(tiles: List<BrowserMenuTile>) {
    tiles.chunked(TILES_PER_ROW).forEach { row ->
        Row(horizontalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.tight)) {
            row.forEach { tile ->
                MenuTile(tile = tile, onClick = tile.onSelect)
            }
        }
    }
}

@Composable
private fun MenuTile(tile: BrowserMenuTile, onClick: () -> Unit) {
    val ink = if (tile.enabled) {
        TaffyTheme.colors.textPrimary
    } else {
        TaffyTheme.colors.hairline
    }
    Column(
        modifier = Modifier
            .width(MenuTileWidth)
            .clip(TaffyTheme.shapes.row)
            .clickable(enabled = tile.enabled, role = Role.Button, onClick = onClick)
            .padding(
                horizontal = TaffyTheme.spacing.step,
                vertical = TaffyTheme.spacing.tight,
            )
            .testTag(tile.testTag)
            .semantics {
                contentDescription = tile.label
                if (!tile.enabled) disabled()
            },
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.tight),
    ) {
        TaffyGlyphFrame(
            modifier = Modifier.testTag(orbTag(tile.testTag)),
            size = MenuOrbSize,
        ) {
            Icon(
                imageVector = tile.icon,
                contentDescription = null,
                modifier = Modifier.size(MenuGlyphSize),
                tint = ink,
            )
        }
        Text(
            text = tile.label,
            style = TaffyTheme.typography.caption,
            color = if (tile.enabled) {
                TaffyTheme.colors.textPrimary
            } else {
                TaffyTheme.colors.hairline
            },
            textAlign = TextAlign.Center,
            maxLines = 2,
            overflow = TextOverflow.Ellipsis,
        )
    }
}

internal fun orbTag(tileTestTag: String): String = "${tileTestTag}_orb"

private const val TILES_PER_ROW = 3

/** This site. */
const val MENU_SITE_HEADING_TEST_TAG: String = "browser_menu_heading_site"

/** Your pages. */
const val MENU_PAGES_HEADING_TEST_TAG: String = "browser_menu_heading_pages"

/** More. */
const val MENU_MORE_HEADING_TEST_TAG: String = "browser_menu_heading_more"

/** Find in page. */
const val FIND_TEST_TAG: String = "browser_menu_find"

/** Share this page. */
const val SHARE_TEST_TAG: String = "browser_menu_share"

/** Save page, in the overflow. Distinct from the sheet's own tag. */
const val SAVE_PAGE_MENU_TEST_TAG: String = "browser_menu_save_page"

/** Ads and trackers. */
const val ADS_TEST_TAG: String = "browser_menu_ads"

/** History. */
const val HISTORY_TEST_TAG: String = "browser_history"

/** Bookmarks. */
const val BOOKMARKS_TEST_TAG: String = "browser_bookmarks"

/** Library. */
const val LIBRARY_TEST_TAG: String = "browser_library"

/** You. */
const val YOU_TEST_TAG: String = "browser_you"

private val MenuTileWidth = 88.dp
private val MenuOrbSize = 48.dp
private val MenuGlyphSize = 21.dp

const val SAVED_FLOWS_MENU_TEST_TAG: String = "browser_menu_saved_flows"

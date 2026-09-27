// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.size
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.SemanticsProperties
import androidx.compose.ui.test.SemanticsMatcher
import androidx.compose.ui.test.assert
import androidx.compose.ui.test.assertHasClickAction
import androidx.compose.ui.test.assertHeightIsEqualTo
import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.assertIsNotEnabled
import androidx.compose.ui.test.getUnclippedBoundsInRoot
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithContentDescription
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.performClick
import androidx.compose.ui.unit.dp
import androidx.compose.ui.test.onNodeWithText
import androidx.test.platform.app.InstrumentationRegistry
import com.taffygo.browser.ui.core.ui.TaffyPreview
import com.taffygo.browser.ui.core.ui.R as CoreUiR
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test

/**
 * Screen SCR-101's chrome: the top bar, the two action rows, and the menu
 * behind the overflow control.
 *
 * Split from [BrowserMainSemanticsTest] along this seam — which chrome a tab
 * gets, and what each control raises — when the two subjects together crossed
 * the file cap. That file keeps what the page area shows; this one keeps what
 * is drawn over it, at either edge of the window.
 */
class BrowserMainChromeSemanticsTest {

    @get:Rule
    val compose = createComposeRule()

    private val intents = mutableListOf<BrowserMainIntent>()

    @Test
    fun theActionRowCarriesTheStableBrowserControls() {
        compose.setContent {
            TaffyPreview(darkTheme = false, reducedMotion = true) {
                BrowserMainContent(
                    state = PreviewStates.browserMain,
                    onIntent = { intents += it },
                    assistantBar = {
                        Box(
                            modifier = Modifier
                                .size(48.dp)
                                .testTag(TEST_ASSISTANT_CONTENT_TAG),
                        )
                    },
                    startComposer = previewStartComposer(),
                )
            }
        }

        // Back, Forward, Assistant, Tabs — one stable line at the bottom. This
        // fixture has no forward history, so Forward stays put but is disabled.
        compose.onNodeWithTag(ACTION_ROW_TEST_TAG).assertExists().assertIsDisplayed()
        listOf(BACK_TEST_TAG, TABS_TEST_TAG).forEach { tag ->
            compose.onNodeWithTag(tag).assertExists().assertHasClickAction()
        }
        compose.onNodeWithTag(FORWARD_TEST_TAG).assertExists().assertIsNotEnabled()
        compose.onNodeWithTag(ASSISTANT_SLOT_TEST_TAG).assertExists().assertIsDisplayed()
        // The address and the one overflow control are the top bar's, above the
        // page rather than under it, and the row below carries no second copy.
        compose.onNodeWithTag(TOP_BAR_TEST_TAG).assertExists().assertIsDisplayed()
        listOf(ADDRESS_BAR_TEST_TAG, MORE_TEST_TAG).forEach { tag ->
            compose.onNodeWithTag(tag).assertExists().assertHasClickAction()
        }
        compose.onNodeWithTag(MORE_MENU_TEST_TAG).assertDoesNotExist()

        compose.onNodeWithTag(TABS_TEST_TAG).performClick()
        compose.onNodeWithTag(MORE_TEST_TAG).performClick()

        assertEquals(
            listOf(BrowserMainIntent.OpenTabSwitcher, BrowserMainIntent.OpenMore),
            intents,
        )
    }

    /**
     * The whole of the split, asserted as geometry rather than as presence.
     *
     * Every other assertion in this file finds a control by tag, and a tag
     * says nothing about where its control was drawn: a top bar composed back
     * into the bottom column would satisfy all of them and the screen would be
     * what it was before decision 0119. This reads the bounds instead — the
     * address and the overflow finish above the row that carries Back,
     * Forward, the Assistant pill and Tabs, with the page between them.
     */
    @Test
    fun theAddressBarIsAtTheTopAndTheActionRowIsAtTheBottom() {
        compose.setContent {
            TaffyPreview(darkTheme = false, reducedMotion = true) {
                BrowserMainContent(
                    state = PreviewStates.browserMain,
                    onIntent = { intents += it },
                    startComposer = previewStartComposer(),
                )
            }
        }

        val topBar = compose.onNodeWithTag(TOP_BAR_TEST_TAG).getUnclippedBoundsInRoot()
        val actionRow = compose.onNodeWithTag(ACTION_ROW_TEST_TAG).getUnclippedBoundsInRoot()
        assertTrue(
            "the top bar must finish above the action row",
            topBar.bottom <= actionRow.top,
        )
        // And the address pill and the overflow are inside that bar, so
        // neither is a copy left behind at the other end of the window.
        listOf(ADDRESS_BAR_TEST_TAG, MORE_TEST_TAG).forEach { tag ->
            val control = compose.onNodeWithTag(tag).getUnclippedBoundsInRoot()
            assertTrue(
                "$tag must be drawn inside the top bar",
                control.top >= topBar.top && control.bottom <= topBar.bottom,
            )
        }
    }

    /**
     * The row under an empty tab is the start page's, not the page's.
     *
     * This screen drew the page row over its own start content, so a tab that
     * had been nowhere came with a Back and a Forward that could never do
     * anything and a Share over a page that was not there — while the same
     * moment reached through SCR-102 was given four slots that all worked.
     * The two are one row now, and this is the assertion that keeps them one.
     */
    @Test
    fun anEmptyTabCarriesTheStartRowRatherThanThePageRow() {
        compose.setContent {
            TaffyPreview(darkTheme = false, reducedMotion = true) {
                BrowserMainContent(
                    state = PreviewStates.browserMainEmptyTab,
                    onIntent = { intents += it },
                    startComposer = previewStartComposer(),
                )
            }
        }

        compose.onNodeWithTag(NEW_TAB_ACTION_ROW_TEST_TAG).assertExists().assertIsDisplayed()
        listOf(
            NEW_TAB_DOWNLOADS_TEST_TAG,
            NEW_TAB_WORKSPACES_TEST_TAG,
            TABS_TEST_TAG,
            NEW_TAB_SETTINGS_TEST_TAG,
        ).forEach { tag ->
            compose.onNodeWithTag(tag).assertExists().assertHasClickAction()
        }

        compose.onNodeWithTag(ACTION_ROW_TEST_TAG).assertDoesNotExist()
        compose.onNodeWithTag(BACK_TEST_TAG).assertDoesNotExist()
        compose.onNodeWithTag(FORWARD_TEST_TAG).assertDoesNotExist()
        compose.onNodeWithTag(MORE_TEST_TAG).assertDoesNotExist()

        // Every dock slot is a plain destination now. The gear used to open
        // the overflow sheet because that sheet was the only route to
        // Downloads on a fresh install; Downloads and Workspaces carry their
        // own slots, so the gear means what its glyph says.
        compose.onNodeWithTag(NEW_TAB_DOWNLOADS_TEST_TAG).performClick()
        compose.onNodeWithTag(NEW_TAB_WORKSPACES_TEST_TAG).performClick()
        compose.onNodeWithTag(NEW_TAB_SETTINGS_TEST_TAG).performClick()

        assertEquals(
            listOf(
                BrowserMainIntent.OpenDownloads,
                BrowserMainIntent.OpenWorkspaces,
                BrowserMainIntent.OpenSettings,
            ),
            intents,
        )
    }

    @Test
    fun forwardHistoryEnablesTheStableForwardTarget() {
        compose.setContent {
            TaffyPreview(darkTheme = false, reducedMotion = true) {
                BrowserMainContent(
                    state = PreviewStates.browserMain.copy(canGoForward = true),
                    onIntent = { intents += it },
                    startComposer = previewStartComposer(),
                )
            }
        }

        compose.onNodeWithTag(FORWARD_TEST_TAG).assertHasClickAction().performClick()

        assertEquals(listOf(BrowserMainIntent.GoForward), intents)
    }

    @Test
    fun everyOtherDestinationIsOneTapBehindTheOverflow() {
        compose.setContent {
            TaffyPreview(darkTheme = false, reducedMotion = true) {
                BrowserMainContent(
                    state = PreviewStates.browserMain.copy(moreOpen = true),
                    onIntent = { intents += it },
                    startComposer = previewStartComposer(),
                )
            }
        }

        compose.onNodeWithTag(MORE_MENU_TEST_TAG).assertExists()
        val heading = SemanticsMatcher.keyIsDefined(SemanticsProperties.Heading)
        listOf(
            MENU_SITE_HEADING_TEST_TAG,
            MENU_PAGES_HEADING_TEST_TAG,
            MENU_MORE_HEADING_TEST_TAG,
        ).forEach { tag ->
            compose.onNodeWithTag(tag).assertExists().assert(heading)
        }
        listOf(
            RELOAD_TEST_TAG,
            FIND_TEST_TAG,
            SHARE_TEST_TAG,
            SAVE_PAGE_MENU_TEST_TAG,
            ADS_TEST_TAG,
            HISTORY_TEST_TAG,
            BOOKMARKS_TEST_TAG,
            DOWNLOADS_TEST_TAG,
            LIBRARY_TEST_TAG,
            WORKSPACES_TEST_TAG,
            YOU_TEST_TAG,
            SETTINGS_TEST_TAG,
        ).forEach { tag ->
            compose.onNodeWithTag(tag).assertExists().assertHasClickAction()
        }
        compose.onNodeWithTag(orbTag(RELOAD_TEST_TAG), useUnmergedTree = true)
            .assertHeightIsEqualTo(48.dp)
        compose.onNodeWithTag(orbTag(SETTINGS_TEST_TAG), useUnmergedTree = true)
            .assertHeightIsEqualTo(48.dp)

        val context = InstrumentationRegistry.getInstrumentation().targetContext
        listOf(
            R.string.taffy_browser_menu_reload,
            R.string.taffy_browser_menu_find,
            R.string.taffy_browser_menu_share,
            R.string.taffy_browser_menu_save_page,
            R.string.taffy_browser_menu_ads,
            R.string.taffy_browser_menu_history,
            R.string.taffy_browser_menu_bookmarks,
            R.string.taffy_browser_downloads,
            R.string.taffy_browser_menu_library,
            R.string.taffy_new_tab_workspaces,
            R.string.taffy_browser_menu_you,
            R.string.taffy_browser_settings,
        ).forEach { id ->
            compose.onNodeWithText(context.getString(id)).assertExists()
        }

        compose.onNodeWithTag(YOU_TEST_TAG).assertIsDisplayed().performClick()

        assertEquals(listOf(BrowserMainIntent.OpenYou), intents)
    }

    @Test
    fun aLoadingPageOffersStopWhereASettledPageOffersReload() {
        compose.setContent {
            TaffyPreview(darkTheme = false, reducedMotion = true) {
                BrowserMainContent(
                    state = PreviewStates.browserMain.copy(
                        isLoading = true,
                        moreOpen = true,
                    ),
                    onIntent = { intents += it },
                    startComposer = previewStartComposer(),
                )
            }
        }

        val context = InstrumentationRegistry.getInstrumentation().targetContext
        compose.onNodeWithContentDescription(
            context.getString(CoreUiR.string.taffy_address_pill_stop),
        ).assertHasClickAction().performClick()
        compose.onNodeWithTag(RELOAD_TEST_TAG).assertDoesNotExist()
        compose.onNodeWithTag(STOP_LOADING_TEST_TAG)
            .assertExists()
            .assertHasClickAction()
            .performClick()
        compose.onNodeWithText(context.getString(R.string.taffy_browser_menu_stop)).assertExists()

        assertEquals(
            listOf(BrowserMainIntent.StopLoading, BrowserMainIntent.StopLoading),
            intents,
        )
    }

    /**
     * One edge of the chrome answers the scroll signal, and the other never
     * does.
     *
     * The field was `actionRowVisible` when the action row was the only chrome
     * on the screen, then `chromeVisible` when both edges moved on it. Both
     * edges moving is the behaviour this assertion now exists to prevent: the
     * action row carries Back, the Assistant pill and the menu, and a page that
     * anchors its own controls to the bottom of its window puts them exactly
     * where that row is. So the row stays, the page is measured above it, and
     * the top bar alone scrolls away.
     */
    @Test
    fun scrollingAwayTakesTheTopBarAndLeavesTheActionRow() {
        compose.setContent {
            TaffyPreview(darkTheme = false, reducedMotion = true) {
                BrowserMainContent(
                    state = PreviewStates.browserMain.copy(topBarVisible = false),
                    onIntent = { intents += it },
                    startComposer = previewStartComposer(),
                )
            }
        }

        compose.onNodeWithTag(TOP_BAR_TEST_TAG).assertDoesNotExist()
        compose.onNodeWithTag(ADDRESS_BAR_TEST_TAG).assertDoesNotExist()
        compose.onNodeWithTag(MORE_TEST_TAG).assertDoesNotExist()
        // The one that must survive every scroll there is.
        compose.onNodeWithTag(ACTION_ROW_TEST_TAG).assertExists()
        // The page is still there: the top bar overlays it, so hiding that bar
        // resizes nothing.
        compose.onNodeWithTag(CONTENT_TEST_TAG).assertExists()
    }

    /**
     * The new tab is unchanged by the split, and this is what says so.
     *
     * The start page centres its own address box in its body and its dock is
     * the four-slot start row. A top bar over either of those would be a
     * second control asking the same question — which is exactly the gate the
     * address strip carried at the bottom, moved unchanged to the new surface.
     * The preparing state is the same assertion one state earlier; it is
     * [theWaitingStateDrawsNoTopBarEither], because one rule takes one content.
     */
    @Test
    fun theStartPageDrawsNoTopBarAtAll() {
        compose.setContent {
            TaffyPreview(darkTheme = false, reducedMotion = true) {
                BrowserMainContent(
                    state = PreviewStates.browserMainEmptyTab,
                    onIntent = { intents += it },
                    startComposer = previewStartComposer(),
                )
            }
        }

        assertNoTopBarOverTheStartRow()
    }

    @Test
    fun theWaitingStateDrawsNoTopBarEither() {
        compose.setContent {
            TaffyPreview(darkTheme = false, reducedMotion = true) {
                BrowserMainContent(
                    state = PreviewStates.browserMainPreparing,
                    onIntent = { intents += it },
                    startComposer = previewStartComposer(),
                )
            }
        }

        assertNoTopBarOverTheStartRow()
    }

    private fun assertNoTopBarOverTheStartRow() {
        compose.onNodeWithTag(TOP_BAR_TEST_TAG).assertDoesNotExist()
        compose.onNodeWithTag(ADDRESS_BAR_TEST_TAG).assertDoesNotExist()
        compose.onNodeWithTag(MORE_TEST_TAG).assertDoesNotExist()
        compose.onNodeWithTag(ACTION_ROW_TEST_TAG).assertDoesNotExist()
        compose.onNodeWithTag(NEW_TAB_ACTION_ROW_TEST_TAG).assertExists()
    }

    @Test
    fun theTabsBadgeIsHiddenWhenOnlyOneTabIsOpen() {
        compose.setContent {
            TaffyPreview(darkTheme = false, reducedMotion = true) {
                BrowserMainContent(
                    state = PreviewStates.browserMainEmptyTab,
                    onIntent = { intents += it },
                    startComposer = previewStartComposer(),
                )
            }
        }

        compose.onNodeWithTag(TABS_BADGE_TEST_TAG).assertDoesNotExist()
        compose.onNodeWithTag(TABS_TEST_TAG).assertExists()
    }

    @Test
    fun aFirstPageKeepsHistoryControlsStableAndDisabled() {
        compose.setContent {
            TaffyPreview(darkTheme = false, reducedMotion = true) {
                BrowserMainContent(
                    state = PreviewStates.browserMain.copy(canGoBack = false),
                    onIntent = { intents += it },
                    assistantBar = {
                        Box(
                            modifier = Modifier
                                .size(48.dp)
                                .testTag(TEST_ASSISTANT_CONTENT_TAG),
                        )
                    },
                    startComposer = previewStartComposer(),
                )
            }
        }

        compose.onNodeWithTag(BACK_TEST_TAG).assertExists().assertIsNotEnabled()
        compose.onNodeWithTag(FORWARD_TEST_TAG).assertExists().assertIsNotEnabled()
        // The stable targets keep the centre Assistant aligned as history
        // changes. Tabs and the application-filled slot remain in the row; the
        // page-actions control is the top bar's and is unaffected by history.
        compose.onNodeWithTag(ACTION_ROW_TEST_TAG).assertExists().assertIsDisplayed()
        compose.onNodeWithTag(TABS_TEST_TAG).assertExists().assertHasClickAction()
        compose.onNodeWithTag(MORE_TEST_TAG).assertExists().assertHasClickAction()
        compose.onNodeWithTag(ASSISTANT_SLOT_TEST_TAG).assertExists().assertIsDisplayed()
        compose.onNodeWithTag(TEST_ASSISTANT_CONTENT_TAG).assertExists().assertIsDisplayed()
    }
}

private const val TEST_ASSISTANT_CONTENT_TAG = "test_assistant_content"

// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import androidx.compose.ui.test.assertContentDescriptionEquals
import androidx.compose.ui.test.assertHasClickAction
import androidx.compose.ui.test.assertTextEquals
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.performClick
import androidx.compose.ui.test.performScrollToIndex
import androidx.test.platform.app.InstrumentationRegistry
import com.taffygo.browser.ui.core.model.TabId
import com.taffygo.browser.ui.core.ui.BACK_TEST_TAG
import com.taffygo.browser.ui.core.ui.EMPTY_STATE_TEST_TAG
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyPreview
import org.junit.Assert.assertEquals
import org.junit.Rule
import org.junit.Test

/**
 * Screen SCR-104 — the tab switcher.
 *
 * The grid says in words everything it says in shapes: which card is showing,
 * which cards Taffy opened, and what it has taken from each of them. Taffy's
 * tabs stay in a group of their own, collapsed by default, so a task that
 * opened four tabs does not look like the user opened four tabs.
 */
class TabSwitcherSemanticsTest {

    @get:Rule
    val compose = createComposeRule()

    private val context = InstrumentationRegistry.getInstrumentation().targetContext
    private val intents = mutableListOf<TabSwitcherIntent>()

    private fun show(state: TabSwitcherUiState, darkTheme: Boolean = false) {
        compose.setContent {
            TaffyPreview(darkTheme = darkTheme, reducedMotion = true) {
                TabSwitcherContent(state = state, onIntent = { intents += it })
            }
        }
    }

    private fun scrollToTaffyGroup(state: TabSwitcherUiState) {
        compose.onNodeWithTag(TAB_GRID_TEST_TAG).performScrollToIndex(state.matchingTabs.size)
    }

    private fun scrollToWorkspaceAction(state: TabSwitcherUiState) {
        val groupItems = when {
            !state.showsTaffyGroup -> 0
            state.taffyGroupExpanded -> state.taffyTabs.size + 1
            else -> 1
        }
        compose.onNodeWithTag(TAB_GRID_TEST_TAG)
            .performScrollToIndex(state.matchingTabs.size + groupItems)
    }

    @Test
    fun theScreenItsGridAndItsControlsAreReachable() {
        show(PreviewStates.tabSwitcher)

        compose.onNodeWithTag(TaffyDestination.TabSwitcher.screenId).assertExists()
        compose.onNodeWithTag(NEW_TAB_TEST_TAG).assertExists().assertHasClickAction()
        compose.onNodeWithTag(COUNTS_TEST_TAG).assertExists()
        compose.onNodeWithTag("${TAB_TEST_TAG_PREFIX}tab_1").assertExists().assertHasClickAction()
        compose.onNodeWithTag("${CLOSE_TEST_TAG_PREFIX}tab_1").assertExists().assertHasClickAction()
        compose.onNodeWithTag(BACK_TEST_TAG).assertDoesNotExist()
        compose.onNodeWithTag(ASK_TAFFY_TEST_TAG).assertExists().assertHasClickAction()
    }

    /** Two parts of a card's description, joined the way the locale joins them. */
    private fun join(left: String, right: String) =
        context.getString(R.string.taffy_tab_switcher_description_join, left, right)

    @Test
    fun theSelectedTabSaysSoInWords() {
        show(PreviewStates.tabSwitcher)

        compose.onNodeWithTag("${TAB_TEST_TAG_PREFIX}tab_1").assertContentDescriptionEquals(
            join(
                join("Retention policy", "docs.example.test"),
                context.getString(R.string.taffy_tab_switcher_showing_now),
            ),
        )
        compose.onNodeWithTag("${TAB_TEST_TAG_PREFIX}tab_2").assertContentDescriptionEquals(
            join("Product listing", "shop.example.test"),
        )
    }

    @Test
    fun aTabThatHasBeenNowhereNamesItselfRatherThanReadingOutAnAddress() {
        show(PreviewStates.tabSwitcher)

        val blank = context.getString(R.string.taffy_tab_switcher_blank_tab)
        // No title, no host, and so exactly one thing to say — never an empty
        // part, and never the internal address the tab is actually on.
        compose.onNodeWithTag("${TAB_TEST_TAG_PREFIX}tab_3")
            .assertExists()
            .assertContentDescriptionEquals(blank)
        compose.onNodeWithTag("${CLOSE_TEST_TAG_PREFIX}tab_3").assertContentDescriptionEquals(
            context.getString(R.string.taffy_tab_switcher_close_description, blank),
        )
        compose.onNodeWithTag("${PREVIEW_TEST_TAG_PREFIX}tab_3", useUnmergedTree = true)
            .assertExists()
        compose.onNodeWithTag("${FAVICON_TEST_TAG_PREFIX}tab_3", useUnmergedTree = true)
            .assertExists()
    }

    @Test
    fun aMissingThumbnailIsASilentSlabRatherThanACaption() {
        show(PreviewStates.tabSwitcher)

        compose.onNodeWithTag("${PREVIEW_TEST_TAG_PREFIX}tab_1", useUnmergedTree = true)
            .assertExists()
    }

    @Test
    fun anOpenDurationChipNamesHowLongTheTabHasBeenOpen() {
        val twoMinutesAgo = System.currentTimeMillis() - 2 * 60_000L
        val cards = PreviewStates.tabSwitcher.yourTabs.map { card ->
            if (card.id.value == "tab_1") card.copy(openedAtEpochMillis = twoMinutesAgo) else card
        }
        show(PreviewStates.tabSwitcher.copy(yourTabs = cards))

        compose.onNodeWithTag("${AGE_TEST_TAG_PREFIX}tab_1", useUnmergedTree = true)
            .assertExists()
            .assertTextEquals(
                context.resources.getQuantityString(
                    R.plurals.taffy_tab_switcher_open_age_minutes,
                    2,
                    2,
                ),
            )
    }

    @Test
    fun askingTaffySendsTheAskIntent() {
        show(PreviewStates.tabSwitcher)

        compose.onNodeWithTag(ASK_TAFFY_TEST_TAG).performClick()

        assertEquals(listOf(TabSwitcherIntent.AskTaffy), intents)
    }

    @Test
    fun closingOneCardNamesTheSiteItCloses() {
        show(PreviewStates.tabSwitcher)

        compose.onNodeWithTag("${CLOSE_TEST_TAG_PREFIX}tab_2").assertContentDescriptionEquals(
            context.getString(R.string.taffy_tab_switcher_close_description, "shop.example.test"),
        )
    }

    @Test
    fun theSegmentedControlSwitchesToPrivateTabs() {
        show(PreviewStates.tabSwitcher)

        compose.onNodeWithTag("${TAB_TEST_TAG_PREFIX}tab_5").assertDoesNotExist()
        compose.onNodeWithTag(GROUP_PRIVATE_TEST_TAG).assertExists().performClick()

        assertEquals(
            listOf(TabSwitcherIntent.SelectGroup(TabSwitcherGroup.PRIVATE)),
            intents,
        )
    }

    @Test
    fun thePrivateSegmentShowsItsOwnTabsAndNoTaffyGroup() {
        show(PreviewStates.tabSwitcher.copy(group = TabSwitcherGroup.PRIVATE))

        compose.onNodeWithTag("${TAB_TEST_TAG_PREFIX}tab_5").assertExists()
        compose.onNodeWithTag("${TAB_TEST_TAG_PREFIX}tab_1").assertDoesNotExist()
        compose.onNodeWithTag(TAFFY_GROUP_TEST_TAG).assertDoesNotExist()
        compose.onNodeWithTag(GROUP_YOURS_TEST_TAG).assertExists()
    }

    @Test
    fun taffysOwnTabsStayInTheirOwnGroupUntilItIsOpened() {
        val state = PreviewStates.tabSwitcher
        show(state)
        scrollToTaffyGroup(state)

        compose.onNodeWithTag("${TAB_TEST_TAG_PREFIX}tab_9").assertDoesNotExist()
        compose.onNodeWithTag(TAFFY_GROUP_TEST_TAG).assertExists().performClick()

        assertEquals(listOf(TabSwitcherIntent.ToggleTaffyGroup), intents)
    }

    @Test
    fun aCollapsedGroupStillExplainsTheAmberEdge() {
        // It used to be explained only inside the opened group, and the edge is
        // drawn on cards in the grid above that group as well. So a collapsed
        // group left an amber outline on screen with nothing anywhere saying
        // what it meant — a status carried by colour alone, which parity row
        // PAR-A11Y-004 forbids.
        val state = PreviewStates.tabSwitcher
        show(state)
        scrollToTaffyGroup(state)

        compose.onNodeWithTag(LEGEND_TEST_TAG).assertExists()
        compose.onNodeWithTag("${TAB_TEST_TAG_PREFIX}tab_9").assertDoesNotExist()
    }

    @Test
    fun anOpenedGroupExplainsTheAmberEdgeRatherThanLeavingItToColour() {
        val state = PreviewStates.tabSwitcher.copy(taffyGroupExpanded = true)
        show(state, darkTheme = true)
        scrollToTaffyGroup(state)

        compose.onNodeWithTag(LEGEND_TEST_TAG).assertExists()
        compose.onNodeWithTag("${TAB_TEST_TAG_PREFIX}tab_9").assertExists()
    }

    @Test
    fun anAmberCardSaysWhoOpenedItAndWhatItGave() {
        val state = PreviewStates.tabSwitcher.copy(taffyGroupExpanded = true)
        show(state)
        scrollToTaffyGroup(state)

        val base = join("Independent review", "reviews.example.test")
        val facts = context.resources.getQuantityString(R.plurals.taffy_tab_switcher_facts, 3, 3)
        compose.onNodeWithTag("${TAB_TEST_TAG_PREFIX}tab_9").assertContentDescriptionEquals(
            join(join(base, context.getString(R.string.taffy_tab_switcher_by_taffy)), facts),
        )
    }

    @Test
    fun aCardTaffyIsStillReadingSaysThatInWordsRatherThanOnlyAsAShape() {
        val state = PreviewStates.tabSwitcher.copy(taffyGroupExpanded = true)
        show(state)
        scrollToTaffyGroup(state)

        val base = join("Specifications", "specs.example.test")
        compose.onNodeWithTag("${TAB_TEST_TAG_PREFIX}tab_10").assertContentDescriptionEquals(
            join(
                join(base, context.getString(R.string.taffy_tab_switcher_by_taffy)),
                context.getString(R.string.taffy_tab_switcher_reading),
            ),
        )
    }

    @Test
    fun selectingAndClosingSendTheirOwnIntents() {
        show(PreviewStates.tabSwitcher)

        compose.onNodeWithTag("${TAB_TEST_TAG_PREFIX}tab_2").performClick()
        compose.onNodeWithTag("${CLOSE_TEST_TAG_PREFIX}tab_2").performClick()

        assertEquals(
            listOf(
                TabSwitcherIntent.Select(TabId("tab_2")),
                TabSwitcherIntent.Close(TabId("tab_2")),
            ),
            intents,
        )
    }

    @Test
    fun theWorkspaceActionSaysThatItOpensThePickerRatherThanBuildingAnything() {
        val state = PreviewStates.tabSwitcher
        show(state)
        scrollToWorkspaceAction(state)

        val label = context.resources.getQuantityString(
            R.plurals.taffy_tab_switcher_new_workspace,
            state.workspaceTabCount,
            state.workspaceTabCount,
        )
        compose.onNodeWithTag(NEW_WORKSPACE_TEST_TAG)
            .assertExists()
            .assertContentDescriptionEquals(
                context.getString(R.string.taffy_tab_switcher_new_workspace_description, label),
            )

        compose.onNodeWithTag(NEW_WORKSPACE_TEST_TAG).performClick()
        assertEquals(listOf(TabSwitcherIntent.StartWorkspace), intents)
    }

    @Test
    fun noTabsIsAStateTheScreenRendersRatherThanABlankArea() {
        show(TabSwitcherUiState())

        compose.onNodeWithTag(EMPTY_STATE_TEST_TAG).assertExists()
        compose.onNodeWithTag(NEW_WORKSPACE_TEST_TAG).assertDoesNotExist()
    }

    @Test
    fun aLargeProfileComposesOnlyTheVisibleTabCards() {
        val cards = (0 until 1_000).map { index ->
            TabCard(
                id = TabId("large_$index"),
                title = "Page $index",
                host = "host-$index.example.test",
            )
        }
        show(TabSwitcherUiState(yourTabs = cards))

        compose.onNodeWithTag("${TAB_TEST_TAG_PREFIX}large_999").assertDoesNotExist()
        compose.onNodeWithTag(TAB_GRID_TEST_TAG).performScrollToIndex(cards.lastIndex)
        compose.onNodeWithTag("${TAB_TEST_TAG_PREFIX}large_999").assertExists()
        compose.onNodeWithTag("${TAB_TEST_TAG_PREFIX}large_0").assertDoesNotExist()
    }

    @Test
    fun anExpandedMaximumTaffyGroupComposesItsLastCardOnlyAfterScrolling() {
        val owned = (0 until BROWSING_CONTRACT_MAX_TABS).map { index ->
            TabCard(
                id = TabId("owned_$index"),
                title = "Owned page $index",
                host = "owned-$index.example.test",
                openedByTaffy = true,
            )
        }
        val state = TabSwitcherUiState(
            yourTabs = listOf(
                TabCard(
                    id = TabId("ordinary"),
                    title = "Ordinary page",
                    host = "ordinary.example.test",
                ),
            ),
            taffyTabs = owned,
            taffyGroupExpanded = true,
        )
        show(state)

        val last = owned.last()
        compose.onNodeWithTag("$TAB_TEST_TAG_PREFIX${last.id.value}").assertDoesNotExist()
        compose.onNodeWithTag(TAB_GRID_TEST_TAG).performScrollToIndex(
            state.matchingTabs.size + 1 + owned.lastIndex,
        )
        compose.onNodeWithTag("$TAB_TEST_TAG_PREFIX${last.id.value}")
            .assertExists()
            .assertHasClickAction()
            .performClick()

        assertEquals(listOf(TabSwitcherIntent.Select(last.id)), intents)
    }
}

// contracts/browsing/schema/contract.json. Kept local because that contract has
// no Kotlin binding; keep it synchronized with any future limit bump.
private const val BROWSING_CONTRACT_MAX_TABS = 256

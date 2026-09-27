// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import androidx.compose.runtime.getValue
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.test.assertContentDescriptionEquals
import androidx.compose.ui.test.assertHasClickAction
import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.captureToImage
import androidx.compose.ui.test.assertTextContains
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.onRoot
import androidx.compose.ui.test.performClick
import androidx.compose.ui.test.performTextInput
import androidx.compose.ui.test.performScrollTo
import androidx.compose.ui.graphics.asAndroidBitmap
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.unit.Density
import androidx.test.platform.app.InstrumentationRegistry
import com.taffygo.browser.ui.core.model.AddressBarInterpretation
import com.taffygo.browser.ui.core.model.TaskAttachedStore
import com.taffygo.browser.ui.core.model.TaskTemplate
import com.taffygo.browser.ui.core.ui.BACK_TEST_TAG as TITLE_BAR_BACK_TEST_TAG
import com.taffygo.browser.ui.core.ui.EMPTY_STATE_TEST_TAG
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyPreview
import com.taffygo.browser.ui.core.ui.TAFFY_START_SCENE_TEST_TAG
import java.io.File
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test

/**
 * Screen SCR-102 — the start page.
 *
 * Built around its middle and with no feed: a greeting over the one box, the
 * person's own most-visited sites under it, and a bottom row that is Tabs and
 * Settings only. A brand new profile has no counted sites, and the screen
 * says that in words rather than showing an empty grid that looks like a
 * defect.
 *
 * The row's composition is the part worth pinning — every slot it does not
 * have is a decision (no Workspaces, no assistant pill, no back, no share),
 * and each is one edit away from drifting back in.
 */
class NewTabSemanticsTest {

    @get:Rule
    val compose = createComposeRule()

    private val context = InstrumentationRegistry.getInstrumentation().targetContext
    private val intents = mutableListOf<NewTabIntent>()

    private fun show(state: NewTabUiState, darkTheme: Boolean = false) {
        compose.setContent {
            TaffyPreview(darkTheme = darkTheme, reducedMotion = true) {
                NewTabContent(
                    state = state,
                    onIntent = { intents += it },
                    composer = previewStartComposer(),
                )
            }
        }
    }

    /**
     * The same screen with a box that actually answers, over a fixed resolver.
     *
     * The stateless half takes its box as a slot, so a test that wants to type
     * holds the box's state itself and runs the real reducer over it — which is
     * the point: what the box shows for a piece of input is
     * [addressBarShowing]'s answer here exactly as it is in the product, rather
     * than a second one written for the test.
     */
    private fun showTypeable(state: NewTabUiState) {
        compose.setContent {
            TaffyPreview(darkTheme = false, reducedMotion = true) {
                var address by remember { mutableStateOf(AddressBarUiState()) }
                NewTabContent(
                    state = state,
                    onIntent = { intents += it },
                    composer = previewStartComposer(address) { intent ->
                        address = reduceAddressBar(address, intent, ::readsAsASite) { emptyList() }
                    },
                )
            }
        }
    }

    /**
     * The box is a box, and everything it offers is on it.
     *
     * It used to be a closed row that opened screen SCR-103 — one control whose
     * whole behaviour was a screen change. It is the composer now: options at
     * the leading edge, the words in the middle, Speak at the trailing one.
     */
    @Test
    fun theBoxIsAFieldWithItsOptionsAndSpeakOnIt() {
        show(PreviewStates.newTab)

        compose.onNodeWithTag(TaffyDestination.NewTab.screenId).assertExists()
        compose.onNodeWithTag(NEW_TAB_ADDRESS_TEST_TAG).assertExists()
        compose.onNodeWithTag(ADDRESS_INPUT_TEST_TAG).assertExists()
        compose.onNodeWithTag(START_OPTIONS_TEST_TAG).assertExists().assertHasClickAction()
        compose.onNodeWithTag(ADDRESS_VOICE_START_TEST_TAG).assertExists().assertHasClickAction()
    }

    @Test
    fun typingInTheBoxChangesNoScreenAndSaysWhatTheWordsMean() {
        showTypeable(PreviewStates.newTab)

        compose.onNodeWithTag(ADDRESS_INPUT_TEST_TAG).performTextInput("taffygo.com")

        // The reading, on this screen, under the box the words were typed into
        // — which is the whole of the change: the resolution is shown before
        // anything consequential runs (ux spec section 5) and it is shown
        // without leaving.
        compose.onNodeWithTag(INTERPRETATION_TEST_TAG).assertExists().assertHasClickAction()
        compose.onNodeWithTag(START_SEND_TEST_TAG).assertExists().assertHasClickAction()
        // Nothing was asked of the screen. Focus and typing are the box's own
        // business; the intent that opens the tab is raised by the stateful
        // half, which a semantics test has no window to build.
        assertEquals(emptyList<NewTabIntent>(), intents)
    }

    /**
     * The welcome is for somebody who has not started yet.
     *
     * With words in the box the lockup, the greeting and the tiles leave, and
     * the room they were using goes to the reading and the suggestions — so a
     * list of them never has to push the field into the keyboard.
     */
    @Test
    fun theGreetingLeavesOnceThereAreWordsInTheBox() {
        showTypeable(PreviewStates.newTab)

        compose.onNodeWithTag(START_GREETING_TEST_TAG).assertExists()
        captureIfRequested("start-scene-light.png")

        compose.onNodeWithTag(ADDRESS_INPUT_TEST_TAG).performTextInput("taffygo.com")

        compose.onNodeWithTag(START_GREETING_TEST_TAG).assertDoesNotExist()
        compose.onNodeWithTag(TAFFY_START_SCENE_TEST_TAG).assertDoesNotExist()
        compose.onNodeWithTag("${FREQUENT_TEST_TAG_PREFIX}docs.example.test").assertDoesNotExist()
        compose.onNodeWithTag(NEW_TAB_ADDRESS_TEST_TAG).assertExists()
    }

    /**
     * The plus is about the question, not about the browser.
     *
     * The person's own stores, the Library, and the shapes a job can take;
     * nothing that is merely somewhere to go. Every absence here is a decision one edit away
     * from drifting back in, so each is asserted rather than assumed.
     */
    @Test
    fun theOptionsControlHoldsThePagesAndTheShapesAndNoDestinations() {
        showTypeable(PreviewStates.newTab)

        compose.onNodeWithTag(START_OPTIONS_TEST_TAG).performClick()

        for (store in TaskAttachedStore.entries) {
            compose.onNodeWithTag("$START_STORE_TILE_TEST_TAG_PREFIX${store.label}")
                .assertExists()
                .assertHasClickAction()
        }
        compose.onNodeWithTag(LIBRARY_TEST_TAG).assertExists().assertHasClickAction()
        compose.onNodeWithTag(START_SHAPES_HEADING_TEST_TAG).assertExists()
        // Somewhere to go is not something a request can be made out of. You
        // and Settings are one line below, in the dock and on the screen the
        // gear opens.
        compose.onNodeWithTag(YOU_TEST_TAG).assertDoesNotExist()
        compose.onNodeWithTag(SETTINGS_TEST_TAG).assertDoesNotExist()
        // The dock's own two are not behind the menu either: decision 0050
        // section 5 gave each of them a slot one line below this control.
        compose.onNodeWithTag(DOWNLOADS_TEST_TAG).assertDoesNotExist()
        compose.onNodeWithTag(WORKSPACES_TEST_TAG).assertDoesNotExist()
    }

    /**
     * A shape stated from the plus becomes a chip, and the chip changes the
     * reading — which is what makes it an option on the question rather than a
     * label beside it.
     */
    @Test
    fun aShapeChosenFromTheMenuBecomesAChipAndChangesWhatTheBoxMeans() {
        showTypeable(PreviewStates.newTab)

        compose.onNodeWithTag(ADDRESS_INPUT_TEST_TAG).performTextInput("taffygo.com")
        compose.onNodeWithTag(START_OPTIONS_TEST_TAG).performClick()
        compose.onNodeWithTag(
            "$START_SHAPE_TILE_TEST_TAG_PREFIX${TaskTemplate.COMPARE_PRODUCTS.label}",
        ).performClick()

        val chip = "$START_SHAPE_CHIP_TEST_TAG_PREFIX${TaskTemplate.COMPARE_PRODUCTS.label}"
        compose.onNodeWithTag(chip).assertExists()
        compose.onNodeWithTag(INTERPRETATION_TEST_TAG).assertTextContains(
            context.getString(R.string.taffy_address_bar_reading_task),
        )
    }

    @Test
    fun theGreetingAndTheGroundAreThere() {
        show(PreviewStates.newTab, darkTheme = true)

        // The greeting rotates through a fixed set, so the test pins the slot
        // rather than one line's text; the backdrop is decoration and is only
        // asserted present, because it must never carry semantics of its own.
        compose.onNodeWithTag(START_GREETING_TEST_TAG).assertExists()
        compose.onNodeWithTag(START_BACKDROP_TEST_TAG).assertExists()
        captureIfRequested("start-scene-dark.png")
    }

    @Test
    fun artworkStandsAboveTheBrandWithoutSharingItsRow() {
        show(PreviewStates.newTab)

        val artwork = compose.onNodeWithTag(TAFFY_START_SCENE_TEST_TAG)
            .assertIsDisplayed().fetchSemanticsNode().boundsInRoot
        val brand = compose.onNodeWithTag(START_BRAND_TEST_TAG)
            .assertIsDisplayed().fetchSemanticsNode().boundsInRoot
        assertTrue("Artwork must finish above the TaffyGo logo", artwork.bottom <= brand.top)
        assertEquals(brand.center.x, artwork.center.x, 1f)
    }

    @Test
    fun largerTextKeepsTheComposerInsteadOfDecorativeArtwork() {
        compose.setContent {
            val density = LocalDensity.current
            CompositionLocalProvider(LocalDensity provides Density(density.density, 2f)) {
                TaffyPreview(darkTheme = true, reducedMotion = true) {
                    NewTabContent(PreviewStates.newTab, {}, composer = previewStartComposer())
                }
            }
        }
        compose.onNodeWithTag(TAFFY_START_SCENE_TEST_TAG).assertDoesNotExist()
        compose.onNodeWithTag(ADDRESS_INPUT_TEST_TAG).performScrollTo().assertIsDisplayed()
        compose.onNodeWithTag(START_OPTIONS_TEST_TAG).assertHasClickAction()
        captureIfRequested("start-scene-large-font.png")
    }

    private fun captureIfRequested(name: String) {
        if (InstrumentationRegistry.getArguments().getString("captureTaskWorkspace") != "true") return
        compose.waitForIdle()
        File(context.getExternalFilesDir(null), name).outputStream().use {
            compose.onRoot().captureToImage().asAndroidBitmap()
                .compress(android.graphics.Bitmap.CompressFormat.PNG, 100, it)
        }
    }

    @Test
    fun aFrequentSiteNamesItsPageAndItsHostAndOpensThatHost() {
        show(PreviewStates.newTab)

        val tile = compose.onNodeWithTag("${FREQUENT_TEST_TAG_PREFIX}docs.example.test")
        tile.assertExists().assertContentDescriptionEquals(
            context.getString(
                R.string.taffy_start_frequent_description,
                "Retention policy",
                "docs.example.test",
            ),
        )

        tile.performClick()
        assertEquals(listOf(NewTabIntent.OpenSite("docs.example.test")), intents)
    }

    @Test
    fun theActionRowOffersTheFourDockDestinationsAndNothingElse() {
        show(PreviewStates.newTab)

        compose.onNodeWithTag(NEW_TAB_ACTION_ROW_TEST_TAG).assertExists()
        listOf(
            NEW_TAB_DOWNLOADS_TEST_TAG,
            NEW_TAB_WORKSPACES_TEST_TAG,
            TABS_TEST_TAG,
            NEW_TAB_SETTINGS_TEST_TAG,
        ).forEach { tag ->
            compose.onNodeWithTag(tag).assertExists().assertHasClickAction()
        }
    }

    @Test
    fun aBlankTabOffersNoHistoryAndNothingToShare() {
        show(PreviewStates.newTab)

        // Back and Forward would be permanently disabled here and Share has
        // nothing to act on, so none of the three is drawn. A control that
        // cannot do its job is worse than an absent one: it looks like it can.
        //
        // Two constants are called BACK_TEST_TAG and this file's own package
        // wins the unqualified one, so the alias is what makes the second
        // assertion say something the first does not: the action row's back
        // control is absent, and so is the title bar's up arrow.
        compose.onNodeWithTag(BACK_TEST_TAG).assertDoesNotExist()
        compose.onNodeWithTag(TITLE_BAR_BACK_TEST_TAG).assertDoesNotExist()
        compose.onNodeWithTag(FORWARD_TEST_TAG).assertDoesNotExist()
        compose.onNodeWithTag(MORE_TEST_TAG).assertDoesNotExist()
    }

    @Test
    fun theTabsControlCountsEveryOpenTabAndNotJustTheFrequentOnes() {
        // Four tabs, three counted sites: the badge counts tabs and the grid
        // shows places, so the two numbers are allowed to differ and do.
        show(PreviewStates.newTab)

        compose.onNodeWithTag(TABS_TEST_TAG).assertContentDescriptionEquals(
            context.resources.getQuantityString(R.plurals.taffy_browser_tabs, 4, 4),
        )
        compose.onNodeWithTag(TABS_BADGE_TEST_TAG).assertExists()
    }

    @Test
    fun theTabsBadgeIsHiddenWhenOnlyOneTabIsOpen() {
        show(
            NewTabUiState(
                userTabCount = 1,
                startPageGate = PreviewStates.newTab.startPageGate,
            ),
        )

        compose.onNodeWithTag(TABS_TEST_TAG).assertContentDescriptionEquals(
            context.resources.getQuantityString(R.plurals.taffy_browser_tabs, 1, 1),
        )
        compose.onNodeWithTag(TABS_BADGE_TEST_TAG).assertDoesNotExist()
    }

    @Test
    fun eachChromeSlotSendsItsOwnIntent() {
        show(PreviewStates.newTab)

        compose.onNodeWithTag(NEW_TAB_DOWNLOADS_TEST_TAG).performClick()
        compose.onNodeWithTag(NEW_TAB_WORKSPACES_TEST_TAG).performClick()
        compose.onNodeWithTag(TABS_TEST_TAG).performClick()
        compose.onNodeWithTag(NEW_TAB_SETTINGS_TEST_TAG).performClick()

        assertEquals(
            listOf(
                NewTabIntent.OpenDownloads,
                NewTabIntent.OpenWorkspaces,
                NewTabIntent.OpenTabSwitcher,
                NewTabIntent.OpenSettings,
            ),
            intents,
        )
    }

    @Test
    fun aProfileThatHasBeenNowhereSaysSo() {
        show(NewTabUiState(startPageGate = StartPageGate(ready = true)), darkTheme = true)

        // One quiet line in the grid's place saying why there is no grid —
        // not a card with a heading of its own, and not tiles gone missing.
        compose.onNodeWithTag(START_FREQUENT_EMPTY_TEST_TAG).assertExists()
        compose.onNodeWithTag(EMPTY_STATE_TEST_TAG).assertDoesNotExist()
        compose.onNodeWithTag("${FREQUENT_TEST_TAG_PREFIX}docs.example.test").assertDoesNotExist()
    }
}

/**
 * One fixed reading, so the test is about the screen rather than about the
 * resolver.
 *
 * The real rule lives in the data layer and has its own tests; what this file
 * has to prove is that whatever the box resolves to reaches the person without
 * a screen change.
 */
private fun readsAsASite(input: String): AddressBarInterpretation =
    AddressBarInterpretation.GoTo(input = input, host = input)

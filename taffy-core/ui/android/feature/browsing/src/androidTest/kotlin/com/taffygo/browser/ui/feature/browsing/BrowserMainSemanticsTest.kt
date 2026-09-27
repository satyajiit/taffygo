// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import androidx.compose.ui.semantics.SemanticsProperties
import androidx.compose.ui.test.SemanticsMatcher
import androidx.compose.ui.test.assert
import androidx.compose.ui.test.assertContentDescriptionContains
import androidx.compose.ui.test.assertContentDescriptionEquals
import androidx.compose.ui.test.assertHasClickAction
import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.performClick
import androidx.test.platform.app.InstrumentationRegistry
import com.taffygo.browser.ui.core.model.BrowserNotice
import com.taffygo.browser.ui.core.model.PageLoadFailure
import com.taffygo.browser.ui.core.ui.TaffyPreview
import org.junit.Assert.assertEquals
import org.junit.Rule
import org.junit.Test

/**
 * Screen SCR-101 in each of its three states, and loading across all of them.
 *
 * Exactly one of failed, start and page is on screen at a time. That is a
 * property of the state type — `BrowserMainUiState.content` — and this test
 * proves the screen honours it rather than drawing two at once.
 *
 * Loading is deliberately not a fourth. It does not replace what the area is
 * drawing; it lights the rail on the address pill and says so as that
 * control's state, which is what the assertions below check instead of the
 * placeholder that used to cover the page.
 *
 * The bottom chrome — which of the two action rows a tab gets, and the menu
 * behind the overflow control — is [BrowserMainChromeSemanticsTest]'s
 * subject, split out when the two together crossed the file cap.
 */
class BrowserMainSemanticsTest {

    @get:Rule
    val compose = createComposeRule()

    private val context = InstrumentationRegistry.getInstrumentation().targetContext
    private val intents = mutableListOf<BrowserMainIntent>()

    @Test
    fun aLoadedPageShowsItsContentAndNeitherOfTheOtherTwoStates() {
        compose.setContent {
            TaffyPreview(darkTheme = false, reducedMotion = true) {
                BrowserMainContent(
                    state = PreviewStates.browserMain,
                    onIntent = { intents += it },
                    startComposer = previewStartComposer(),
                )
            }
        }

        compose.onNodeWithTag(CONTENT_TEST_TAG)
            .assertExists()
            .assertContentDescriptionEquals("Retention policy")
        compose.onNodeWithTag(FAILURE_TEST_TAG).assertDoesNotExist()
        compose.onNodeWithTag(ADDRESS_BAR_TEST_TAG).assert(notLoading())
    }

    @Test
    fun somethingTheBrowserRefusedIsSaidInWordsAndCanBeReadAndDismissed() {
        compose.setContent {
            TaffyPreview(darkTheme = false, reducedMotion = true) {
                BrowserMainContent(
                    state = PreviewStates.browserMain.copy(
                        notice = BrowserNotice.NO_SEARCH_ENGINE,
                    ),
                    onIntent = { intents += it },
                    startComposer = previewStartComposer(),
                )
            }
        }

        // The page is untouched: the notice sits beside it, not over it.
        compose.onNodeWithTag(NOTICE_TEST_TAG).assertExists().assertIsDisplayed()
        compose.onNodeWithTag(CONTENT_TEST_TAG).assertExists()
        compose.onNodeWithTag(FAILURE_TEST_TAG).assertDoesNotExist()

        compose.onNodeWithTag(NOTICE_DISMISS_TEST_TAG).assertExists().performClick()
        assertEquals(listOf(BrowserMainIntent.DismissNotice), intents)
    }

    @Test
    fun aBrowserWithNothingToExplainShowsNoNotice() {
        compose.setContent {
            TaffyPreview(darkTheme = false, reducedMotion = true) {
                BrowserMainContent(
                    state = PreviewStates.browserMain,
                    onIntent = { intents += it },
                    startComposer = previewStartComposer(),
                )
            }
        }

        compose.onNodeWithTag(NOTICE_TEST_TAG).assertDoesNotExist()
    }

    @Test
    fun aLoadingPageKeepsTheReadablePageAndSaysSoOnTheAddressPill() {
        compose.setContent {
            TaffyPreview(darkTheme = false, reducedMotion = true) {
                BrowserMainContent(
                    state = PreviewStates.browserMain.copy(isLoading = true),
                    onIntent = { intents += it },
                    startComposer = previewStartComposer(),
                )
            }
        }

        // The defect this replaces: an opaque skeleton stood where the page
        // was, so every navigation blanked something the reader could still
        // read. The page stays, and the pill carries the fact instead.
        compose.onNodeWithTag(CONTENT_TEST_TAG).assertExists()
        compose.onNodeWithTag(ADDRESS_BAR_TEST_TAG).assert(loading())
    }

    @Test
    fun aFailedPageShowsTheNoticeInsteadOfContent() {
        compose.setContent {
            TaffyPreview(darkTheme = true, reducedMotion = true) {
                BrowserMainContent(
                    state = PreviewStates.browserMain.copy(failure = PageLoadFailure.OFFLINE),
                    onIntent = { intents += it },
                    startComposer = previewStartComposer(),
                )
            }
        }

        compose.onNodeWithTag(FAILURE_TEST_TAG).assertExists()
        compose.onNodeWithTag(CONTENT_TEST_TAG).assertDoesNotExist()
    }

    // -----------------------------------------------------------------------
    // A tab with no page.
    //
    // The defect these cover: the page area drew the engine's own surface over
    // a blank document, which in the dark theme is a white slab across two
    // thirds of the display. The screen had no state for "this tab has been
    // nowhere", so it drew the one state it had.
    // -----------------------------------------------------------------------

    @Test
    fun aTabThatHasBeenNowhereDrawsTaffysOwnStartContent() {
        compose.setContent {
            TaffyPreview(darkTheme = true, reducedMotion = true) {
                BrowserMainContent(
                    state = PreviewStates.browserMainEmptyTab,
                    onIntent = { intents += it },
                    startComposer = previewStartComposer(),
                )
            }
        }

        compose.onNodeWithTag(START_TEST_TAG).assertExists().assertIsDisplayed()
        compose.onNodeWithTag(START_GREETING_TEST_TAG).assertExists()
        // One of the three, never two: the page's own area is not drawn beside
        // an invitation to open a page.
        compose.onNodeWithTag(CONTENT_TEST_TAG).assertDoesNotExist()
        compose.onNodeWithTag(FAILURE_TEST_TAG).assertDoesNotExist()
    }

    @Test
    fun anEmptyPrivateTabSaysWhichTabItIsInThePageAreaToo() {
        compose.setContent {
            TaffyPreview(darkTheme = true, reducedMotion = true) {
                BrowserMainContent(
                    state = PreviewStates.browserMainEmptyPrivateTab,
                    onIntent = { intents += it },
                    startComposer = previewStartComposer(),
                )
            }
        }

        // A fresh private tab is this area and the band below it. The start
        // page's greeting carries no word about privacy; the band is what says
        // the tab is private, and it is the only thing on the screen that
        // says so.
        compose.onNodeWithTag(START_TEST_TAG).assertExists()
        compose.onNodeWithTag(START_GREETING_TEST_TAG).assertExists()
        compose.onNodeWithTag(PRIVATE_TEST_TAG).assertExists()
    }

    @Test
    fun anEmptyTabThatIsAlreadyFetchingKeepsItsStartContent() {
        compose.setContent {
            TaffyPreview(darkTheme = false, reducedMotion = true) {
                BrowserMainContent(
                    state = PreviewStates.browserMainEmptyTab.copy(isLoading = true),
                    onIntent = { intents += it },
                    startComposer = previewStartComposer(),
                )
            }
        }

        // The one place a cover is still right: this tab has committed nothing,
        // so beneath the start content is the engine's blank document and not a
        // page. Taking the invitation away here would show that instead — a
        // white slab — which is the defect this section exists for.
        compose.onNodeWithTag(START_TEST_TAG).assertExists()
    }

    // The start page centres the one address entry in its own body, so the
    // chrome's own pill stays away while it is up: two identical boxes would
    // be two controls asking the same question. It was the bottom strip that
    // stayed away; it is the top bar now, and the gate did not move with it.
    //
    // The box is typed into where it stands (decision 0131), so it is a field
    // and its controls are on it: the options at the leading edge, Speak at the
    // trailing one. Pressing it starts nothing — a screen change here is the
    // exact behaviour this design removed, and an intent raised from the box
    // would be that screen change coming back.
    @Test
    fun anEmptyTabCentresTheOneAddressEntryAndDrawsNoChromePill() {
        compose.setContent {
            TaffyPreview(darkTheme = false, reducedMotion = true) {
                BrowserMainContent(
                    state = PreviewStates.browserMainEmptyTab,
                    onIntent = { intents += it },
                    startComposer = previewStartComposer(),
                )
            }
        }

        compose.onNodeWithTag(ADDRESS_BAR_TEST_TAG).assertDoesNotExist()
        compose.onNodeWithTag(NEW_TAB_ADDRESS_TEST_TAG).assertExists()
        compose.onNodeWithTag(ADDRESS_INPUT_TEST_TAG).assertExists()
        compose.onNodeWithTag(START_OPTIONS_TEST_TAG).assertExists().assertHasClickAction()
        compose.onNodeWithTag(ADDRESS_VOICE_START_TEST_TAG).assertExists().assertHasClickAction()

        compose.onNodeWithTag(ADDRESS_INPUT_TEST_TAG).performClick()

        assertEquals(emptyList<BrowserMainIntent>(), intents)
    }

    @Test
    fun anEmptyTabShowsThePersonsFrequentSites() {
        compose.setContent {
            TaffyPreview(darkTheme = false, reducedMotion = true) {
                BrowserMainContent(
                    state = PreviewStates.browserMainEmptyTab.copy(
                        frequent = PreviewStates.newTab.frequent,
                    ),
                    onIntent = { intents += it },
                    startComposer = previewStartComposer(),
                )
            }
        }

        val tile = compose.onNodeWithTag("${FREQUENT_TEST_TAG_PREFIX}docs.example.test")
        tile.assertHasClickAction().performClick()

        assertEquals(listOf(BrowserMainIntent.OpenSite("docs.example.test")), intents)
    }

    @Test
    fun aTabWithAPageDrawsNoStartContent() {
        compose.setContent {
            TaffyPreview(darkTheme = false, reducedMotion = true) {
                BrowserMainContent(
                    state = PreviewStates.browserMain,
                    onIntent = { intents += it },
                    startComposer = previewStartComposer(),
                )
            }
        }

        compose.onNodeWithTag(START_TEST_TAG).assertDoesNotExist()
    }

    @Test
    fun theAddressBarIsOneTargetThatSaysWhereYouAre() {
        compose.setContent {
            TaffyPreview(darkTheme = false, reducedMotion = true) {
                BrowserMainContent(
                    state = PreviewStates.browserMain,
                    onIntent = { intents += it },
                    startComposer = previewStartComposer(),
                )
            }
        }

        compose.onNodeWithTag(ADDRESS_BAR_TEST_TAG)
            .assertExists()
            .assertHasClickAction()
            // The pill is one target, so the connection's own description
            // merges into it: the element says both where you are and that the
            // connection is secure. This asserts the half this module owns —
            // the other half is :core:ui's string and is not this module's to
            // declare.
            .assertContentDescriptionContains(
                context.getString(R.string.taffy_browser_address_description, "docs.example.test"),
            )

        compose.onNodeWithTag(ADDRESS_BAR_TEST_TAG).performClick()

        assertEquals(listOf(BrowserMainIntent.FocusAddressBar), intents)
    }

    // -----------------------------------------------------------------------
    // Which tab this is.
    //
    // A private tab used to look exactly like every other one here: no badge,
    // no colour, and nothing in the accessibility tree either. These are the
    // two halves of the answer — the one you can see, and the one you are told.
    // -----------------------------------------------------------------------

    @Test
    fun aPrivateTabSaysSoAboveTheAddressBar() {
        compose.setContent {
            TaffyPreview(darkTheme = false, reducedMotion = true) {
                BrowserMainContent(
                    state = PreviewStates.browserMainPrivate,
                    onIntent = { intents += it },
                    startComposer = previewStartComposer(),
                )
            }
        }

        compose.onNodeWithTag(PRIVATE_TEST_TAG)
            .assertExists()
            .assertIsDisplayed()
            // The promise itself, not only the violet: a band that does not say
            // what it means is a promise nobody made.
            .assertContentDescriptionEquals(
                context.getString(
                    R.string.taffy_browser_private_description,
                    context.getString(R.string.taffy_browser_private_title),
                    context.getString(R.string.taffy_browser_private_body),
                ),
            )
    }

    @Test
    fun thePrivateAddressBarSaysWhichTabItBelongsTo() {
        compose.setContent {
            TaffyPreview(darkTheme = false, reducedMotion = true) {
                BrowserMainContent(
                    state = PreviewStates.browserMainPrivate,
                    onIntent = { intents += it },
                    startComposer = previewStartComposer(),
                )
            }
        }

        // The control the person is about to type into, reached directly by the
        // address-bar gesture without passing the band above it.
        compose.onNodeWithTag(ADDRESS_BAR_TEST_TAG)
            .assertExists()
            .assertContentDescriptionContains(
                context.getString(
                    R.string.taffy_browser_address_description_private,
                    "docs.example.test",
                ),
            )
    }

    @Test
    fun anOrdinaryTabCarriesNoPrivatePromise() {
        compose.setContent {
            TaffyPreview(darkTheme = false, reducedMotion = true) {
                BrowserMainContent(
                    state = PreviewStates.browserMain,
                    onIntent = { intents += it },
                    startComposer = previewStartComposer(),
                )
            }
        }

        compose.onNodeWithTag(PRIVATE_TEST_TAG).assertDoesNotExist()
        compose.onNodeWithTag(ADDRESS_BAR_TEST_TAG).assertContentDescriptionContains(
            context.getString(R.string.taffy_browser_address_description, "docs.example.test"),
        )
    }

    /** The address pill says a page is on its way, in the words a reader hears. */
    private fun loading() = SemanticsMatcher.expectValue(
        SemanticsProperties.StateDescription,
        context.getString(R.string.taffy_browser_loading),
    )

    /**
     * The pill carries no state at all when nothing is loading.
     *
     * Asserted as the absence of the key rather than as some other value. A
     * control that always had a state description would make the loading one
     * unremarkable, and a screen reader would read something on every landing.
     */
    private fun notLoading() =
        SemanticsMatcher.keyIsDefined(SemanticsProperties.StateDescription).not()
}

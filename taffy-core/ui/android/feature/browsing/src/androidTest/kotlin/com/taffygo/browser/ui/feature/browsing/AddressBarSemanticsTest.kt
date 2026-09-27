// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import androidx.compose.ui.test.assertContentDescriptionEquals
import androidx.compose.ui.test.assertHasClickAction
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.performClick
import androidx.compose.ui.test.performImeAction
import androidx.compose.ui.test.performTextReplacement
import androidx.test.platform.app.InstrumentationRegistry
import com.taffygo.browser.ui.core.model.AddressBarCommand
import com.taffygo.browser.ui.core.model.AddressBarInterpretation
import com.taffygo.browser.ui.core.model.Suggestion
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyPreview
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test

/**
 * Screen SCR-103 — the one box, and what it thinks you meant.
 *
 * The reading is shown in words before anything happens, so a person is never
 * surprised by a task starting when they meant to search. This test checks the
 * reading is announced, not merely drawn.
 */
class AddressBarSemanticsTest {

    @get:Rule
    val compose = createComposeRule()

    private val context = InstrumentationRegistry.getInstrumentation().targetContext
    private val intents = mutableListOf<AddressBarIntent>()

    @Test
    fun theReadingIsAnnouncedInWordsBeforeAnythingHappens() {
        compose.setContent {
            TaffyPreview(darkTheme = false, reducedMotion = true) {
                AddressBarContent(state = PreviewStates.addressBar, onIntent = { intents += it })
            }
        }

        compose.onNodeWithTag(INTERPRETATION_TEST_TAG)
            .assertExists()
            .assertHasClickAction()
            .assertContentDescriptionEquals(
                context.getString(
                    R.string.taffy_address_bar_interpretation_description,
                    context.getString(R.string.taffy_address_bar_reading_task),
                    "compare these two policies",
                ),
            )
    }

    @Test
    fun typingSendsTheTextAndNothingElse() {
        compose.setContent {
            TaffyPreview(darkTheme = false, reducedMotion = true) {
                AddressBarContent(state = AddressBarUiState(), onIntent = { intents += it })
            }
        }

        compose.onNodeWithTag(ADDRESS_INPUT_TEST_TAG).performTextReplacement("docs.example.test")

        assertEquals(
            listOf(AddressBarIntent.InputChanged("docs.example.test")),
            intents,
        )
    }

    @Test
    fun choosingTheReadingSendsTheReadingItself() {
        compose.setContent {
            TaffyPreview(darkTheme = false, reducedMotion = true) {
                AddressBarContent(state = PreviewStates.addressBar, onIntent = { intents += it })
            }
        }

        compose.onNodeWithTag(INTERPRETATION_TEST_TAG).performClick()

        val chosen = intents.single() as AddressBarIntent.Choose
        assertTrue(chosen.interpretation is AddressBarInterpretation.TaskForTaffy)
    }

    /**
     * The keyboard's own key, on the same path as the check mark.
     *
     * The field used to declare no IME action at all, so the newline was
     * swallowed and pressing enter did nothing anywhere in TaffyGo. What this
     * asserts is not merely that something happens: it is that the key sends
     * `Choose`, carrying the reading the screen is already showing, so enter
     * can no more start something unannounced than the mark can.
     */
    @Test
    fun theKeyboardsActionSendsTheSameReadingTheCheckMarkWouldSend() {
        compose.setContent {
            TaffyPreview(darkTheme = false, reducedMotion = true) {
                AddressBarContent(state = PreviewStates.addressBar, onIntent = { intents += it })
            }
        }

        compose.onNodeWithTag(ADDRESS_INPUT_TEST_TAG).performImeAction()

        val chosen = intents.single() as AddressBarIntent.Choose
        assertEquals(PreviewStates.addressBar.interpretation, chosen.interpretation)
    }

    @Test
    fun eachSuggestionSaysWhatItIsAndWhereItCameFrom() {
        compose.setContent {
            TaffyPreview(darkTheme = true, reducedMotion = true) {
                AddressBarContent(state = PreviewStates.addressBar, onIntent = { intents += it })
            }
        }

        // The first suggestion is the resolver's own reading, which the
        // interpretation row above already says; it is not drawn twice.
        compose.onNodeWithTag("${SUGGESTION_TEST_TAG_PREFIX}suggestion_0").assertDoesNotExist()
        // A reading row is titled by the reading and carries the words beside
        // where they would go, so what choosing it does is said before the
        // words are.
        val readingSupporting = context.getString(
            R.string.taffy_address_bar_suggestion_supporting,
            context.getString(R.string.taffy_address_bar_source_search),
            "compare these two policies",
        )
        compose.onNodeWithTag("${SUGGESTION_TEST_TAG_PREFIX}suggestion_1")
            .assertExists()
            .assertContentDescriptionEquals(
                context.getString(
                    R.string.taffy_address_bar_suggestion_description,
                    context.getString(R.string.taffy_address_bar_reading_search),
                    readingSupporting,
                ),
            )
    }

    @Test
    fun commandAndSavedPageRowsSpeakTheirLocalizedSource() {
        val state = AddressBarUiState(
            input = "open",
            interpretation = AddressBarInterpretation.Search("open"),
            suggestions = listOf(
                Suggestion(
                    id = "clear",
                    title = "clear browsing data",
                    interpretation = AddressBarInterpretation.BrowserCommand(
                        "clear browsing data",
                        AddressBarCommand.OPEN_CLEAR_BROWSING_DATA,
                    ),
                    source = Suggestion.Source.BROWSER_COMMAND,
                ),
                Suggestion(
                    id = "saved",
                    title = "Saved policy",
                    interpretation = AddressBarInterpretation.GoTo(
                        "https://docs.example.test/policy",
                        "docs.example.test",
                    ),
                    source = Suggestion.Source.BOOKMARK,
                    supportingText = "docs.example.test",
                ),
            ),
        )
        compose.setContent {
            TaffyPreview(darkTheme = false, reducedMotion = true) {
                AddressBarContent(state = state, onIntent = { intents += it })
            }
        }

        compose.onNodeWithTag("${SUGGESTION_TEST_TAG_PREFIX}clear")
            .assertContentDescriptionEquals(
                context.getString(
                    R.string.taffy_address_bar_suggestion_description,
                    context.getString(R.string.taffy_address_bar_command_clear_data),
                    context.getString(R.string.taffy_address_bar_source_command),
                ),
            )
        val savedSupporting = context.getString(
            R.string.taffy_address_bar_suggestion_supporting,
            context.getString(R.string.taffy_address_bar_source_bookmark),
            "docs.example.test",
        )
        compose.onNodeWithTag("${SUGGESTION_TEST_TAG_PREFIX}saved")
            .assertContentDescriptionEquals(
                context.getString(
                    R.string.taffy_address_bar_suggestion_description,
                    "Saved policy",
                    savedSupporting,
                ),
            )
    }

    @Test
    fun anEmptyBoxOffersNoReadingToActOn() {
        compose.setContent {
            TaffyPreview(darkTheme = false, reducedMotion = true) {
                AddressBarContent(state = AddressBarUiState(), onIntent = { intents += it })
            }
        }

        compose.onNodeWithTag(TaffyDestination.AddressBar.screenId).assertExists()
        compose.onNodeWithTag(INTERPRETATION_TEST_TAG).assertDoesNotExist()
    }
}

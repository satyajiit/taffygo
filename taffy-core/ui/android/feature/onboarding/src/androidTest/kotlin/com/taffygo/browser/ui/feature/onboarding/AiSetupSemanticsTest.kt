// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.onboarding

import androidx.compose.ui.test.assertIsEnabled
import androidx.compose.ui.test.assertIsNotEnabled
import androidx.compose.ui.test.assertIsNotSelected
import androidx.compose.ui.test.assertIsSelected
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.performClick
import com.taffygo.browser.ui.core.model.ProviderRoute
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyPreview
import org.junit.Assert.assertEquals
import org.junit.Rule
import org.junit.Test

/**
 * Screen SCR-004 — AI setup.
 *
 * The screen that decides whether anything can leave the device at all, so
 * the assertions are about the default: nothing chosen, the one route
 * offered, and a way past that is not a route.
 */
class AiSetupSemanticsTest {

    @get:Rule
    val compose = createComposeRule()

    private val intents = mutableListOf<AiSetupIntent>()

    @Test
    fun nothingIsChosenUntilSomethingIsChosen() {
        compose.setContent {
            TaffyPreview(darkTheme = false, reducedMotion = true) {
                AiSetupContent(state = AiSetupUiState(), onIntent = { intents += it })
            }
        }

        compose.onNodeWithTag(TaffyDestination.AiSetup.screenId).assertExists()
        compose.onNodeWithTag(
            "$AI_SETUP_ROUTE_TEST_TAG_PREFIX${ProviderRoute.DIRECT_WITH_YOUR_KEY.label}",
        ).assertIsNotSelected()
    }

    @Test
    fun theChosenRouteSaysSo() {
        compose.setContent {
            TaffyPreview(darkTheme = true, reducedMotion = true) {
                AiSetupContent(
                    state = AiSetupUiState(route = ProviderRoute.DIRECT_WITH_YOUR_KEY),
                    onIntent = { intents += it },
                )
            }
        }

        compose.onNodeWithTag(
            "$AI_SETUP_ROUTE_TEST_TAG_PREFIX${ProviderRoute.DIRECT_WITH_YOUR_KEY.label}",
        ).assertIsSelected()
    }

    @Test
    fun continueStaysOffUntilARouteIsChosen() {
        compose.setContent {
            TaffyPreview(darkTheme = false, reducedMotion = true) {
                AiSetupContent(state = AiSetupUiState(), onIntent = { intents += it })
            }
        }

        compose.onNodeWithTag(AI_SETUP_FOOTER_NOTE_TEST_TAG).assertExists()
        compose.onNodeWithTag(AI_SETUP_PRIMARY_TEST_TAG).assertIsNotEnabled()
        assertEquals(emptyList<AiSetupIntent>(), intents)
    }

    @Test
    fun theSameActionCarriesAChosenRouteOut() {
        compose.setContent {
            TaffyPreview(darkTheme = true, reducedMotion = true) {
                AiSetupContent(
                    state = AiSetupUiState(route = ProviderRoute.DIRECT_WITH_YOUR_KEY),
                    onIntent = { intents += it },
                )
            }
        }

        compose.onNodeWithTag(AI_SETUP_PRIMARY_TEST_TAG).performClick()

        assertEquals(listOf(AiSetupIntent.Finish), intents)
    }

    @Test
    fun theOwnKeyRouteIsARealChoiceRatherThanAnInformationalCard() {
        compose.setContent {
            TaffyPreview(darkTheme = false, reducedMotion = true) {
                AiSetupContent(state = AiSetupUiState(), onIntent = { intents += it })
            }
        }

        compose.onNodeWithTag(
            "$AI_SETUP_ROUTE_TEST_TAG_PREFIX${ProviderRoute.DIRECT_WITH_YOUR_KEY.label}",
        ).performClick()

        assertEquals(
            listOf(AiSetupIntent.ChooseRoute(ProviderRoute.DIRECT_WITH_YOUR_KEY)),
            intents,
        )
    }

    /**
     * The way past is available from the state a person arrives in, which is
     * the whole of what "skippable" means. A skip that needs a selection first
     * is not one.
     */
    @Test
    fun settingUpLaterIsAvailableWithNothingChosen() {
        compose.setContent {
            TaffyPreview(darkTheme = false, reducedMotion = true) {
                AiSetupContent(state = AiSetupUiState(), onIntent = { intents += it })
            }
        }

        compose.onNodeWithTag(AI_SETUP_SKIP_TEST_TAG).assertIsEnabled()
        compose.onNodeWithTag(AI_SETUP_SKIP_TEST_TAG).performClick()

        assertEquals(listOf(AiSetupIntent.SetUpLater), intents)
    }

    /**
     * Two controls, two meanings. The failure this guards against is the
     * primary action quietly becoming the skip when nothing is selected, which
     * would make one button mean two things depending on state nobody re-reads.
     */
    @Test
    fun theSkipIsNotThePrimaryActionWearingAnotherLabel() {
        compose.setContent {
            TaffyPreview(darkTheme = true, reducedMotion = true) {
                AiSetupContent(state = AiSetupUiState(), onIntent = { intents += it })
            }
        }

        compose.onNodeWithTag(AI_SETUP_PRIMARY_TEST_TAG).assertIsNotEnabled()
        compose.onNodeWithTag(AI_SETUP_SKIP_TEST_TAG).assertIsEnabled()
    }

    @Test
    fun whatHoldsEitherWayIsOnTheScreen() {
        compose.setContent {
            TaffyPreview(darkTheme = true, reducedMotion = true) {
                AiSetupContent(state = AiSetupUiState(), onIntent = { intents += it })
            }
        }

        compose.onNodeWithTag(AI_SETUP_ROUTES_TEST_TAG).assertExists()
        compose.onNodeWithTag(AI_SETUP_FOOTNOTE_TEST_TAG).assertExists()
    }
}

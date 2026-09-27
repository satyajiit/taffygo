// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.onboarding

import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.assertIsNotSelected
import androidx.compose.ui.test.assertIsSelected
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.performClick
import com.taffygo.browser.ui.core.model.ThemePreference
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyPreview
import org.junit.Assert.assertEquals
import org.junit.Rule
import org.junit.Test

/**
 * Screen SCR-001 — Welcome.
 *
 * The screen has one forward action, plus locale and appearance preferences.
 */
class WelcomeSemanticsTest {

    @get:Rule
    val compose = createComposeRule()

    private val intents = mutableListOf<WelcomeIntent>()

    @Test
    fun thePromiseAndTheOneActionAreBothOnScreen() {
        compose.setContent {
            TaffyPreview(darkTheme = true, reducedMotion = true) {
                WelcomeContent(state = WelcomeUiState(), onIntent = { intents += it })
            }
        }

        compose.onNodeWithTag(TaffyDestination.OnboardingWelcome.screenId).assertExists()
        compose.onNodeWithTag(WELCOME_BADGE_TEST_TAG).assertExists()
        compose.onNodeWithTag(WELCOME_HEADLINE_TEST_TAG).assertExists()
        compose.onNodeWithTag(WELCOME_PROMISE_TEST_TAG).assertExists()
        compose.onNodeWithTag(LANGUAGE_CHIP_TEST_TAG).assertExists()
        // A fresh profile follows the system, and the compact pair shows that
        // as whichever appearance the theme resolved to. Rendered dark here,
        // so the honest selection is Dark: the screen the person is looking
        // at *is* dark, and a switch that said otherwise would be lying.
        compose.onNodeWithTag(WELCOME_THEME_DARK_TEST_TAG).assertIsSelected()
        compose.onNodeWithTag(WELCOME_THEME_LIGHT_TEST_TAG).assertIsNotSelected()
        compose.onNodeWithTag(WELCOME_START_TEST_TAG).assertIsDisplayed()
    }

    @Test
    fun theOneActionAsksToStart() {
        compose.setContent {
            TaffyPreview(darkTheme = true, reducedMotion = true) {
                WelcomeContent(state = WelcomeUiState(), onIntent = { intents += it })
            }
        }

        compose.onNodeWithTag(WELCOME_START_TEST_TAG).performClick()

        assertEquals(listOf(WelcomeIntent.StartBrowsing), intents)
    }

    @Test
    fun appearanceCanBeChosenWithoutLeavingWelcome() {
        compose.setContent {
            TaffyPreview(darkTheme = true, reducedMotion = true) {
                WelcomeContent(state = WelcomeUiState(), onIntent = { intents += it })
            }
        }

        compose.onNodeWithTag(WELCOME_THEME_LIGHT_TEST_TAG).performClick()

        assertEquals(listOf(WelcomeIntent.ChooseTheme(ThemePreference.LIGHT)), intents)
    }

    @Test
    fun aPinnedThemeIsShownAsPinnedWhateverTheSystemResolvesTo() {
        // A profile pinned to Light stays Light even while the preview frame
        // is rendered dark: the pin is the person's, and the resolved system
        // appearance may no longer override what they chose. (The way back to
        // follow-the-system is the settings Appearance screen, which offers
        // all three values in words.)
        compose.setContent {
            TaffyPreview(darkTheme = true, reducedMotion = true) {
                WelcomeContent(
                    state = WelcomeUiState(theme = ThemePreference.LIGHT),
                    onIntent = { intents += it },
                )
            }
        }

        compose.onNodeWithTag(WELCOME_THEME_LIGHT_TEST_TAG).assertIsSelected()
        compose.onNodeWithTag(WELCOME_THEME_DARK_TEST_TAG).assertIsNotSelected().performClick()

        assertEquals(listOf(WelcomeIntent.ChooseTheme(ThemePreference.DARK)), intents)
    }
}

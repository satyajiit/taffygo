// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import androidx.compose.ui.test.assertIsNotSelected
import androidx.compose.ui.test.assertIsOff
import androidx.compose.ui.test.assertIsOn
import androidx.compose.ui.test.assertIsSelected
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.performClick
import com.taffygo.browser.ui.core.model.AppLanguage
import com.taffygo.browser.ui.core.model.ThemePreference
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyPreview
import org.junit.Assert.assertEquals
import org.junit.Rule
import org.junit.Test

/**
 * Screen SCR-407 — Appearance.
 *
 * Text scaling is the system's setting rather than a second one here, and the
 * screen says where to change it. The pseudo-localization switch is the
 * instrument parity row PAR-L10N-001 asks for, and it is reachable. Country
 * and language are independent: Hindi and SYSTEM are offered outside India.
 */
class AppearanceSemanticsTest {

    @get:Rule
    val compose = createComposeRule()

    private val intents = mutableListOf<AppearanceIntent>()

    @Test
    fun everyThemeIsOfferedAndTheChosenOneSaysSo() {
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                AppearanceContent(
                    state = AppearanceUiState(theme = ThemePreference.DARK),
                    onIntent = { intents += it },
                )
            }
        }

        compose.onNodeWithTag(TaffyDestination.Appearance.screenId).assertExists()
        ThemePreference.entries.forEach { theme ->
            compose.onNodeWithTag("$THEME_TEST_TAG_PREFIX${theme.label}").assertExists()
        }
        compose.onNodeWithTag("$THEME_TEST_TAG_PREFIX${ThemePreference.DARK.label}")
            .assertIsSelected()
        compose.onNodeWithTag("$THEME_TEST_TAG_PREFIX${ThemePreference.LIGHT.label}")
            .assertIsNotSelected()
    }

    @Test
    fun choosingAThemeSendsThatTheme() {
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                AppearanceContent(
                    state = AppearanceUiState(),
                    onIntent = { intents += it },
                )
            }
        }

        compose.onNodeWithTag("$THEME_TEST_TAG_PREFIX${ThemePreference.LIGHT.label}").performClick()

        assertEquals(listOf(AppearanceIntent.ChooseTheme(ThemePreference.LIGHT)), intents)
    }

    @Test
    fun everyLanguageIsOfferedAndTheChosenOneSaysSo() {
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                AppearanceContent(
                    state = AppearanceUiState(
                        tab = AppearanceTab.LANGUAGE,
                        appLanguage = AppLanguage.HINDI,
                    ),
                    onIntent = { intents += it },
                )
            }
        }

        AppLanguage.entries.forEach { language ->
            compose.onNodeWithTag("$LANGUAGE_TEST_TAG_PREFIX${language.label}").assertExists()
        }
        compose.onNodeWithTag("$LANGUAGE_TEST_TAG_PREFIX${AppLanguage.HINDI.label}")
            .assertIsSelected()
        compose.onNodeWithTag("$LANGUAGE_TEST_TAG_PREFIX${AppLanguage.ENGLISH.label}")
            .assertIsNotSelected()
    }

    @Test
    fun choosingALanguageSendsThatLanguage() {
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                AppearanceContent(
                    state = AppearanceUiState(tab = AppearanceTab.LANGUAGE),
                    onIntent = { intents += it },
                )
            }
        }

        compose.onNodeWithTag("$LANGUAGE_TEST_TAG_PREFIX${AppLanguage.HINDI.label}").performClick()

        assertEquals(listOf(AppearanceIntent.ChooseLanguage(AppLanguage.HINDI)), intents)
    }

    @Test
    fun hindiAndSystemAreOfferedOutsideIndia() {
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                AppearanceContent(
                    state = AppearanceUiState(
                        tab = AppearanceTab.LANGUAGE,
                        regionCode = "US",
                        appLanguage = AppLanguage.HINDI,
                    ),
                    onIntent = { intents += it },
                )
            }
        }

        AppLanguage.entries.forEach { language ->
            compose.onNodeWithTag("$LANGUAGE_TEST_TAG_PREFIX${language.label}").assertExists()
        }
        compose.onNodeWithTag("$LANGUAGE_TEST_TAG_PREFIX${AppLanguage.HINDI.label}")
            .assertIsSelected()
        compose.onNodeWithTag(COUNTRY_PILL_TEST_TAG).assertExists()
    }

    @Test
    fun followTheSystemStaysSelectedOutsideIndia() {
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                AppearanceContent(
                    state = AppearanceUiState(
                        tab = AppearanceTab.LANGUAGE,
                        regionCode = "JP",
                        appLanguage = AppLanguage.SYSTEM,
                    ),
                    onIntent = { intents += it },
                )
            }
        }

        compose.onNodeWithTag("$LANGUAGE_TEST_TAG_PREFIX${AppLanguage.SYSTEM.label}")
            .assertIsSelected()
        compose.onNodeWithTag("$LANGUAGE_TEST_TAG_PREFIX${AppLanguage.ENGLISH.label}")
            .assertIsNotSelected()
    }

    @Test
    fun theCountryPillOpensTheSearchableSheet() {
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                AppearanceContent(
                    state = AppearanceUiState(tab = AppearanceTab.LANGUAGE),
                    onIntent = { intents += it },
                )
            }
        }

        compose.onNodeWithTag(COUNTRY_PILL_TEST_TAG).performClick()

        assertEquals(listOf(AppearanceIntent.OpenRegionPicker), intents)
    }

    @Test
    fun aCountryFromTheCompleteListCanBeChosen() {
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                AppearanceContent(
                    state = AppearanceUiState(
                        regionPickerVisible = true,
                        regionSearchQuery = "Japan",
                    ),
                    onIntent = { intents += it },
                )
            }
        }

        compose.onNodeWithTag("${COUNTRY_TEST_TAG_PREFIX}JP").performClick()

        assertEquals(listOf(AppearanceIntent.ChooseRegion("JP")), intents)
    }

    @Test
    fun theScreenSaysWhereTextSizeIsChanged() {
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                AppearanceContent(
                    state = AppearanceUiState(),
                    onIntent = { intents += it },
                )
            }
        }

        compose.onNodeWithTag(TEXT_SIZE_NOTE_TEST_TAG).assertExists()
    }

    @Test
    fun thePseudoLocalizationSwitchIsReachableAndReportsItsState() {
        compose.setContent {
            TaffyPreview(darkTheme = true) {
                AppearanceContent(
                    state = AppearanceUiState(
                        tab = AppearanceTab.LANGUAGE,
                        pseudoLocalization = true,
                    ),
                    onIntent = { intents += it },
                )
            }
        }

        compose.onNodeWithTag(PSEUDO_SWITCH_TEST_TAG).assertIsOn().performClick()

        assertEquals(listOf(AppearanceIntent.TogglePseudoLocalization), intents)
    }

    @Test
    fun choosingLanguageTabSendsThatTab() {
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                AppearanceContent(
                    state = AppearanceUiState(),
                    onIntent = { intents += it },
                )
            }
        }

        compose.onNodeWithTag("${APPEARANCE_TAB_TEST_TAG_PREFIX}LANGUAGE").performClick()

        assertEquals(listOf(AppearanceIntent.SelectTab(AppearanceTab.LANGUAGE)), intents)
    }

    @Test
    fun darkSitesSwitchIsOnTheThemeTab() {
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                AppearanceContent(
                    state = AppearanceUiState(forceDarkWeb = true),
                    onIntent = { intents += it },
                )
            }
        }

        compose.onNodeWithTag(FORCE_DARK_SWITCH_TEST_TAG).assertIsOn().performClick()

        assertEquals(listOf(AppearanceIntent.ToggleForceDarkWeb), intents)
    }

    @Test
    fun theSwitchIsOffUntilItIsTurnedOn() {
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                AppearanceContent(
                    state = AppearanceUiState(tab = AppearanceTab.LANGUAGE),
                    onIntent = { intents += it },
                )
            }
        }

        compose.onNodeWithTag(PSEUDO_SWITCH_TEST_TAG).assertIsOff()
    }
}

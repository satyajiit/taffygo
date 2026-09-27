// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.onboarding

import androidx.compose.ui.semantics.SemanticsProperties
import androidx.compose.ui.test.SemanticsMatcher
import androidx.compose.ui.test.assert
import androidx.compose.ui.test.assertIsNotSelected
import androidx.compose.ui.test.assertIsSelected
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.performClick
import androidx.compose.ui.text.input.ImeAction
import com.taffygo.browser.ui.core.model.AppLanguage
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyPreview
import org.junit.Assert.assertEquals
import org.junit.Rule
import org.junit.Test

/**
 * Screen SCR-006 — Language and region.
 *
 * Both language and country choices are real. The country sheet is backed by
 * the platform ISO catalog. Hindi and Follow the system are offered in every
 * country.
 */
class LanguageRegionSemanticsTest {

    @get:Rule
    val compose = createComposeRule()

    private val intents = mutableListOf<LanguageRegionIntent>()

    @Test
    fun everyLanguageIsOfferedAndTheChosenOneSaysSo() {
        compose.setContent {
            TaffyPreview(darkTheme = false, reducedMotion = true) {
                LanguageRegionContent(
                    state = LanguageRegionUiState(appLanguage = AppLanguage.HINDI),
                    onIntent = { intents += it },
                )
            }
        }

        compose.onNodeWithTag(TaffyDestination.LanguageRegion.screenId).assertExists()
        listOf(AppLanguage.ENGLISH, AppLanguage.HINDI).forEach { language ->
            compose.onNodeWithTag("$LANGUAGE_REGION_LANGUAGE_TEST_TAG_PREFIX${language.label}")
                .assertExists()
        }
        compose.onNodeWithTag("$LANGUAGE_REGION_LANGUAGE_TEST_TAG_PREFIX${AppLanguage.HINDI.label}")
            .assertIsSelected()
        compose.onNodeWithTag(
            "$LANGUAGE_REGION_LANGUAGE_TEST_TAG_PREFIX${AppLanguage.ENGLISH.label}",
        ).assertIsNotSelected()
    }

    @Test
    fun choosingALanguageSendsThatLanguage() {
        compose.setContent {
            TaffyPreview(darkTheme = false, reducedMotion = true) {
                LanguageRegionContent(
                    state = LanguageRegionUiState(),
                    onIntent = { intents += it },
                )
            }
        }

        compose.onNodeWithTag("$LANGUAGE_REGION_LANGUAGE_TEST_TAG_PREFIX${AppLanguage.HINDI.label}")
            .performClick()

        assertEquals(listOf(LanguageRegionIntent.ChooseLanguage(AppLanguage.HINDI)), intents)
    }

    @Test
    fun hindiAndSystemAreOfferedOutsideIndia() {
        compose.setContent {
            TaffyPreview(darkTheme = false, reducedMotion = true) {
                LanguageRegionContent(
                    state = LanguageRegionUiState(
                        regionCode = "US",
                        appLanguage = AppLanguage.HINDI,
                    ),
                    onIntent = { intents += it },
                )
            }
        }

        AppLanguage.entries.forEach { language ->
            compose.onNodeWithTag("$LANGUAGE_REGION_LANGUAGE_TEST_TAG_PREFIX${language.label}")
                .assertExists()
        }
        compose.onNodeWithTag("$LANGUAGE_REGION_LANGUAGE_TEST_TAG_PREFIX${AppLanguage.HINDI.label}")
            .assertIsSelected()
    }

    @Test
    fun theCompleteRegionPickerCanBeOpened() {
        compose.setContent {
            TaffyPreview(darkTheme = true, reducedMotion = true) {
                LanguageRegionContent(
                    state = LanguageRegionUiState(),
                    onIntent = { intents += it },
                )
            }
        }

        compose.onNodeWithTag(LANGUAGE_REGION_REGION_TEST_TAG).assertExists()
        compose.onNodeWithTag(LANGUAGE_REGION_OPEN_PICKER_TEST_TAG).performClick()

        assertEquals(listOf(LanguageRegionIntent.OpenRegionPicker), intents)
    }

    /**
     * Both search fields on this screen ask the keyboard for Search rather
     * than taking the single-line default of Done. Each filters its list as
     * the person types, so Search is what the key is honestly for, and neither
     * has a button beside it that the key could disagree with.
     */
    @Test
    fun bothSearchFieldsOfferSearchOnTheKeyboard() {
        compose.setContent {
            TaffyPreview(darkTheme = false, reducedMotion = true) {
                LanguageRegionContent(
                    state = LanguageRegionUiState(regionPickerVisible = true),
                    onIntent = { intents += it },
                )
            }
        }

        val search = SemanticsMatcher.expectValue(SemanticsProperties.ImeAction, ImeAction.Search)
        compose.onNodeWithTag(LANGUAGE_REGION_SEARCH_TEST_TAG).assert(search)
        compose.onNodeWithTag(LANGUAGE_REGION_PICKER_SEARCH_TEST_TAG).assert(search)
    }

    @Test
    fun aCountryFromTheCompleteListCanBeChosen() {
        compose.setContent {
            TaffyPreview(darkTheme = false, reducedMotion = true) {
                LanguageRegionContent(
                    state = LanguageRegionUiState(
                        regionPickerVisible = true,
                        regionSearchQuery = "Japan",
                    ),
                    onIntent = { intents += it },
                )
            }
        }

        compose.onNodeWithTag("${LANGUAGE_REGION_COUNTRY_TEST_TAG_PREFIX}JP").performClick()

        assertEquals(listOf(LanguageRegionIntent.ChooseRegion("JP")), intents)
    }
}

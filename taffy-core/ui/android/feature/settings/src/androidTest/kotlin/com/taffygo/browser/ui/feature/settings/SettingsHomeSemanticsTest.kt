// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import androidx.compose.ui.semantics.SemanticsProperties
import androidx.compose.ui.test.SemanticsMatcher
import androidx.compose.ui.test.assert
import androidx.compose.ui.test.assertHasClickAction
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performClick
import androidx.compose.ui.test.performTextReplacement
import androidx.compose.ui.text.input.ImeAction
import androidx.test.platform.app.InstrumentationRegistry
import com.taffygo.browser.ui.core.ui.EMPTY_STATE_TEST_TAG
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyPreview
import org.junit.Assert.assertEquals
import org.junit.Rule
import org.junit.Test

/**
 * Screen SCR-401 — identity card, toolbar search CTA, and a labeled list.
 */
class SettingsHomeSemanticsTest {

    @get:Rule
    val compose = createComposeRule()

    private val context = InstrumentationRegistry.getInstrumentation().targetContext
    private val intents = mutableListOf<SettingsHomeIntent>()

    @Test
    fun everyHomeRowIsARowThatOpensSomething() {
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                SettingsHomeContent(state = SettingsHomeUiState(), onIntent = { intents += it })
            }
        }

        compose.onNodeWithTag(TaffyDestination.SettingsHome.screenId).assertExists()
        compose.onNodeWithTag(SETTINGS_LIST_TEST_TAG).assertExists()
        compose.onNodeWithTag(SETTINGS_GRID_TEST_TAG).assertDoesNotExist()
        SettingsSection.entries.filter { it.listedOnHome }.forEach { section ->
            compose.onNodeWithTag("$SETTINGS_SECTION_TEST_TAG_PREFIX${section.name}")
                .assertExists()
                .assertHasClickAction()
        }
    }

    @Test
    fun adsAndTrackersIsTheBlockingTitle() {
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                SettingsHomeContent(state = SettingsHomeUiState(), onIntent = { intents += it })
            }
        }

        compose.onNodeWithText(
            context.getString(R.string.taffy_settings_blocking_title),
            useUnmergedTree = true,
        ).assertExists()
    }

    @Test
    fun openingASectionSendsThatDestination() {
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                SettingsHomeContent(state = SettingsHomeUiState(), onIntent = { intents += it })
            }
        }

        compose
            .onNodeWithTag("$SETTINGS_SECTION_TEST_TAG_PREFIX${SettingsSection.APPEARANCE.name}")
            .performClick()

        assertEquals(listOf(SettingsHomeIntent.Open(TaffyDestination.Appearance)), intents)
    }

    @Test
    fun theIdentityCardOpensYou() {
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                SettingsHomeContent(state = SettingsHomeUiState(), onIntent = { intents += it })
            }
        }

        compose.onNodeWithTag(SETTINGS_HEADER_TEST_TAG).assertExists().assertHasClickAction()
        // A phone with no name set draws the screen's own name, not a
        // placeholder person and not a line about an account.
        compose.onNodeWithText(context.getString(R.string.taffy_settings_you_title))
            .assertExists()
        compose.onNodeWithTag(SETTINGS_HEADER_TEST_TAG).performClick()
        assertEquals(listOf(SettingsHomeIntent.Open(TaffyDestination.You)), intents)
    }

    @Test
    fun theToolbarSearchCtaTogglesTheField() {
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                SettingsHomeContent(state = SettingsHomeUiState(), onIntent = { intents += it })
            }
        }

        compose.onNodeWithTag(SETTINGS_SEARCH_TEST_TAG).assertDoesNotExist()
        compose.onNodeWithTag(SETTINGS_SEARCH_ACTION_TEST_TAG).assertExists().performClick()
        assertEquals(listOf(SettingsHomeIntent.ToggleSearch), intents)
    }

    @Test
    fun aQueryWithoutTheCtaDoesNotShowTheField() {
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                SettingsHomeContent(
                    state = SettingsHomeUiState(query = "password"),
                    onIntent = { intents += it },
                )
            }
        }

        compose.onNodeWithTag(SETTINGS_SEARCH_TEST_TAG).assertDoesNotExist()
        compose.onNodeWithTag(SETTINGS_HEADER_TEST_TAG).assertDoesNotExist()
        compose.onNodeWithTag(SETTINGS_LIST_TEST_TAG).assertExists()
    }

    @Test
    fun homeShowsLabeledGroupsNotAGrid() {
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                SettingsHomeContent(state = SettingsHomeUiState(), onIntent = { intents += it })
            }
        }

        compose.onNodeWithText(
            context.getString(R.string.taffy_settings_group_phone),
            ignoreCase = true,
        ).assertExists()
        compose.onNodeWithTag(SETTINGS_GRID_TEST_TAG).assertDoesNotExist()
        compose.onNodeWithTag(SETTINGS_HEADER_TEST_TAG).assertExists()
    }

    @Test
    fun theSearchNarrowsToWhatItMatches() {
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                SettingsHomeContent(
                    state = SettingsHomeUiState(
                        query = context.getString(R.string.taffy_settings_appearance_title),
                    ),
                    onIntent = { intents += it },
                )
            }
        }

        compose.onNodeWithTag(SETTINGS_GRID_TEST_TAG).assertDoesNotExist()
        compose.onNodeWithTag(SETTINGS_LIST_TEST_TAG).assertExists()
        compose
            .onNodeWithTag("$SETTINGS_SECTION_TEST_TAG_PREFIX${SettingsSection.APPEARANCE.name}")
            .assertExists()
        compose
            .onNodeWithTag("$SETTINGS_SECTION_TEST_TAG_PREFIX${SettingsSection.NOTIFICATIONS.name}")
            .assertDoesNotExist()
    }

    @Test
    fun searchingPasswordFindsSavedSignIns() {
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                SettingsHomeContent(
                    state = SettingsHomeUiState(query = "password"),
                    onIntent = { intents += it },
                )
            }
        }

        compose
            .onNodeWithTag("$SETTINGS_SECTION_TEST_TAG_PREFIX${SettingsSection.SAVED_SIGN_INS.name}")
            .assertExists()
        compose
            .onNodeWithTag("$SETTINGS_SECTION_TEST_TAG_PREFIX${SettingsSection.YOU.name}")
            .assertExists()
    }

    @Test
    fun typingSendsTheQuery() {
        compose.setContent {
            TaffyPreview(darkTheme = true) {
                SettingsHomeContent(
                    state = SettingsHomeUiState(searchOpen = true),
                    onIntent = { intents += it },
                )
            }
        }

        compose.onNodeWithTag(SETTINGS_SEARCH_TEST_TAG).performTextReplacement("notify")

        assertEquals(listOf(SettingsHomeIntent.QueryChanged("notify")), intents)
    }

    @Test
    fun aSearchThatMatchesNothingSaysSo() {
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                SettingsHomeContent(
                    state = SettingsHomeUiState(query = "a printer"),
                    onIntent = { intents += it },
                )
            }
        }

        compose.onNodeWithTag(EMPTY_STATE_TEST_TAG).assertExists()
        compose.onNodeWithTag(SETTINGS_LIST_TEST_TAG).assertDoesNotExist()
        compose.onNodeWithTag(SETTINGS_GRID_TEST_TAG).assertDoesNotExist()
    }

    @Test
    fun theSearchFieldOffersSearchOnTheKeyboard() {
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                SettingsHomeContent(
                    state = SettingsHomeUiState(searchOpen = true),
                    onIntent = { intents += it },
                )
            }
        }

        compose.onNodeWithTag(SETTINGS_SEARCH_TEST_TAG).assert(
            SemanticsMatcher.expectValue(SemanticsProperties.ImeAction, ImeAction.Search),
        )
    }
}

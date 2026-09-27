// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import androidx.compose.ui.test.assertHasClickAction
import androidx.compose.ui.test.assertIsNotSelected
import androidx.compose.ui.test.assertIsSelected
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.performClick
import com.taffygo.browser.ui.core.ui.EMPTY_STATE_TEST_TAG
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyPreview
import org.junit.Assert.assertEquals
import org.junit.Rule
import org.junit.Test

/**
 * Screen SCR-603 — preset cards, not a second assistant.
 */
class PersonalitySemanticsTest {

    @get:Rule
    val compose = createComposeRule()

    private val intents = mutableListOf<PersonalityIntent>()

    @Test
    fun everyPresetIsACardAndTheChosenOneSaysSo() {
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                PersonalityContent(state = readyState(), onIntent = { intents += it })
            }
        }

        compose.onNodeWithTag(TaffyDestination.Personality.screenId).assertExists()
        compose.onNodeWithTag(PERSONALITY_INTRO_TEST_TAG).assertExists()
        compose.onNodeWithTag(
            "$PERSONALITY_PRESET_TEST_TAG_PREFIX${PersonalityRepository.Preset.CAREFUL_RESEARCHER.name}",
        ).assertIsSelected()
        compose.onNodeWithTag(
            "$PERSONALITY_PRESET_TEST_TAG_PREFIX${PersonalityRepository.Preset.QUICK_SHOPPER.name}",
        ).assertIsNotSelected().assertHasClickAction()
        compose.onNodeWithTag(PERSONALITY_TUNE_ROW_TEST_TAG).assertHasClickAction()
    }

    @Test
    fun choosingAPresetAndOpeningTuningSendThoseIntents() {
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                PersonalityContent(state = readyState(), onIntent = { intents += it })
            }
        }

        compose.onNodeWithTag(
            "$PERSONALITY_PRESET_TEST_TAG_PREFIX${PersonalityRepository.Preset.TRIP_PLANNER.name}",
        ).performClick()
        compose.onNodeWithTag(PERSONALITY_TUNE_ROW_TEST_TAG).performClick()

        assertEquals(
            listOf(
                PersonalityIntent.ChoosePreset(PersonalityRepository.Preset.TRIP_PLANNER),
                PersonalityIntent.OpenTuning,
            ),
            intents,
        )
    }

    @Test
    fun unavailableKeepsThePresetsAsCopyAndNamesTheGap() {
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                PersonalityContent(
                    state = PersonalityUiState(
                        availability = PersonalityRepository.Availability.UNAVAILABLE,
                        choiceNotSaved = true,
                    ),
                    onIntent = { intents += it },
                )
            }
        }

        compose.onNodeWithTag(EMPTY_STATE_TEST_TAG).assertExists()
        compose.onNodeWithTag(PERSONALITY_NOT_SAVED_TEST_TAG).assertExists()
        compose.onNodeWithTag(
            "$PERSONALITY_PRESET_TEST_TAG_PREFIX${PersonalityRepository.Preset.CAREFUL_RESEARCHER.name}",
        ).assertExists()
    }

    private fun readyState() = PersonalityUiState(
        availability = PersonalityRepository.Availability.READY,
        selected = PersonalityRepository.Preset.CAREFUL_RESEARCHER,
    )
}

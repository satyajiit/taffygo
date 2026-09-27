// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import androidx.compose.ui.test.assertIsSelected
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.performClick
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyPreview
import org.junit.Assert.assertEquals
import org.junit.Rule
import org.junit.Test

/**
 * Screen SCR-604 — three scales that change how Taffy talks, never what it may do.
 */
class PersonalityTuningSemanticsTest {

    @get:Rule
    val compose = createComposeRule()

    private val intents = mutableListOf<PersonalityTuningIntent>()

    @Test
    fun everyAxisIsOnScreenAndTheChosenEndSaysSo() {
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                PersonalityTuningContent(
                    state = PersonalityTuningUiState(
                        availability = PersonalityRepository.Availability.READY,
                        scales = PersonalityRepository.Scales(pace = 0, length = 1, checkIn = 2),
                    ),
                    onIntent = { intents += it },
                )
            }
        }

        compose.onNodeWithTag(TaffyDestination.PersonalityTuning.screenId).assertExists()
        compose.onNodeWithTag(TUNING_CAPTION_TEST_TAG).assertExists()
        compose.onNodeWithTag(TUNING_PACE_TEST_TAG).assertExists()
        compose.onNodeWithTag(TUNING_LENGTH_TEST_TAG).assertExists()
        compose.onNodeWithTag(TUNING_CHECKIN_TEST_TAG).assertExists()
        compose.onNodeWithTag("${TUNING_PACE_OPTION_TEST_TAG_PREFIX}0").assertIsSelected()
        compose.onNodeWithTag("${TUNING_LENGTH_OPTION_TEST_TAG_PREFIX}1").assertIsSelected()
        compose.onNodeWithTag("${TUNING_CHECKIN_OPTION_TEST_TAG_PREFIX}2").assertIsSelected()
    }

    @Test
    fun choosingAnEndSendsThatScale() {
        compose.setContent {
            TaffyPreview(darkTheme = true) {
                PersonalityTuningContent(
                    state = PersonalityTuningUiState(
                        availability = PersonalityRepository.Availability.READY,
                    ),
                    onIntent = { intents += it },
                )
            }
        }

        compose.onNodeWithTag("${TUNING_PACE_OPTION_TEST_TAG_PREFIX}2").performClick()

        assertEquals(
            listOf(
                PersonalityTuningIntent.SetScale(PersonalityTuningIntent.Axis.PACE, 2),
            ),
            intents,
        )
    }

    @Test
    fun unavailableNamesThatPersonalityIsNotSaved() {
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                PersonalityTuningContent(
                    state = PersonalityTuningUiState(
                        availability = PersonalityRepository.Availability.UNAVAILABLE,
                        notSaved = true,
                    ),
                    onIntent = { intents += it },
                )
            }
        }

        compose.onNodeWithTag(TUNING_NOT_SAVED_TEST_TAG).assertExists()
        compose.onNodeWithTag(TUNING_CAPTION_TEST_TAG).assertExists()
    }
}

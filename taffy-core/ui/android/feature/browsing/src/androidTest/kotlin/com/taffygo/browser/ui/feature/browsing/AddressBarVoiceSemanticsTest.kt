// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import androidx.compose.ui.test.assertHasClickAction
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performClick
import androidx.test.platform.app.InstrumentationRegistry
import com.taffygo.browser.ui.core.ui.TaffyPreview
import com.taffygo.browser.ui.core.ui.VoiceEntryState
import com.taffygo.browser.ui.core.ui.VoiceTranscript
import org.junit.Assert.assertEquals
import org.junit.Rule
import org.junit.Test

class AddressBarVoiceSemanticsTest {
    @get:Rule
    val compose = createComposeRule()

    private val context = InstrumentationRegistry.getInstrumentation().targetContext
    private val intents = mutableListOf<AddressBarIntent>()

    @Test
    fun voiceEntryStartsOnlyFromItsVisibleControl() {
        compose.setContent {
            TaffyPreview(darkTheme = false, reducedMotion = true) {
                AddressBarContent(
                    state = AddressBarUiState(),
                    onIntent = { intents += it },
                )
            }
        }

        compose.onNodeWithTag(ADDRESS_VOICE_START_TEST_TAG)
            .assertHasClickAction()
            .performClick()

        assertEquals(listOf(AddressBarIntent.StartVoiceInput), intents)
    }

    @Test
    fun finalWordsAreVisibleWithPrivacyCopyAndASeparateConfirmation() {
        val words = requireNotNull(VoiceTranscript.bounded("open the saved policy"))
        compose.setContent {
            TaffyPreview(darkTheme = false, reducedMotion = true) {
                AddressBarContent(
                    state = AddressBarUiState(
                        voiceEntry = VoiceEntryState.Review(words),
                    ),
                    onIntent = { intents += it },
                )
            }
        }

        compose.onNodeWithTag(ADDRESS_VOICE_OVERLAY_TEST_TAG).assertExists()
        compose.onNodeWithTag(ADDRESS_VOICE_TRANSCRIPT_TEST_TAG).assertExists()
        compose.onNodeWithText("open the saved policy").assertExists()
        compose.onNodeWithTag(ADDRESS_VOICE_PRIVACY_TEST_TAG).assertExists()
        compose.onNodeWithText(context.getString(R.string.taffy_address_voice_privacy)).assertExists()
        compose.onNodeWithTag(ADDRESS_VOICE_CONFIRM_TEST_TAG).performClick()

        assertEquals(listOf(AddressBarIntent.ConfirmVoiceInput), intents)
    }

    @Test
    fun listeningCanAlwaysBeCancelled() {
        compose.setContent {
            TaffyPreview(darkTheme = false, reducedMotion = true) {
                AddressBarContent(
                    state = AddressBarUiState(
                        voiceEntry = VoiceEntryState.Listening(),
                    ),
                    onIntent = { intents += it },
                )
            }
        }

        compose.onNodeWithTag(ADDRESS_VOICE_STATUS_TEST_TAG).assertExists()
        compose.onNodeWithTag(ADDRESS_VOICE_CANCEL_TEST_TAG).performClick()

        assertEquals(listOf(AddressBarIntent.CancelVoiceInput), intents)
    }
}

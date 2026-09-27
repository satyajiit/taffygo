// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import androidx.compose.ui.test.assertHasClickAction
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.performClick
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyPreview
import org.junit.Assert.assertEquals
import org.junit.Rule
import org.junit.Test

/**
 * Screen SCR-405 — Taffy hub rows each open a real child.
 */
class TaffySettingsSemanticsTest {

    @get:Rule
    val compose = createComposeRule()

    private val intents = mutableListOf<TaffySettingsIntent>()

    @Test
    fun theHubNamesTaffyAndTheThreeChildren() {
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                TaffySettingsContent(
                    state = TaffySettingsUiState(),
                    onIntent = { intents += it },
                )
            }
        }

        compose.onNodeWithTag(TaffyDestination.TaffySettings.screenId).assertExists()
        compose.onNodeWithTag(TAFFY_HUB_LIST_TEST_TAG).assertExists()
        compose.onNodeWithTag(TAFFY_HUB_TALK_TEST_TAG).assertExists().assertHasClickAction()
        compose.onNodeWithTag(TAFFY_HUB_SKILLS_TEST_TAG).assertExists().assertHasClickAction()
        compose.onNodeWithTag(TAFFY_HUB_AI_TEST_TAG).assertExists().assertHasClickAction()
    }

    @Test
    fun eachRowSendsItsChild() {
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                TaffySettingsContent(
                    state = TaffySettingsUiState(),
                    onIntent = { intents += it },
                )
            }
        }

        compose.onNodeWithTag(TAFFY_HUB_TALK_TEST_TAG).performClick()
        compose.onNodeWithTag(TAFFY_HUB_SKILLS_TEST_TAG).performClick()
        compose.onNodeWithTag(TAFFY_HUB_AI_TEST_TAG).performClick()

        assertEquals(
            listOf(
                TaffySettingsIntent.OpenPersonality,
                TaffySettingsIntent.OpenSkills,
                TaffySettingsIntent.OpenAiProviders,
            ),
            intents,
        )
    }
}

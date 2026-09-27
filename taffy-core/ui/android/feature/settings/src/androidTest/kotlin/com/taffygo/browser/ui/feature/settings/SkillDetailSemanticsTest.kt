// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import androidx.compose.ui.test.assertIsOff
import androidx.compose.ui.test.assertIsOn
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
 * Screen SCR-602 — what it does, what it may use, on/off, honest remove.
 */
class SkillDetailSemanticsTest {

    @get:Rule
    val compose = createComposeRule()

    private val intents = mutableListOf<SkillDetailIntent>()

    @Test
    fun aBuiltInShowsWhatItDoesAndADisabledRemove() {
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                SkillDetailContent(state = builtInState(), onIntent = { intents += it })
            }
        }

        compose.onNodeWithTag(
            TaffyDestination.SkillDetail(SkillsRepository.FORM_ASSISTANT).screenId,
        )
            .assertExists()
        compose.onNodeWithTag(SKILL_DETAIL_DOES_TEST_TAG).assertExists()
        compose.onNodeWithTag(SKILL_DETAIL_MAY_USE_TEST_TAG).assertExists()
        compose.onNodeWithTag(SKILL_DETAIL_APPROVALS_TEST_TAG).assertExists()
        compose.onNodeWithTag(SKILL_DETAIL_VERSION_TEST_TAG).assertExists()
        compose.onNodeWithTag(SKILL_DETAIL_SWITCH_TEST_TAG).assertIsOn()
        compose.onNodeWithTag(SKILL_DETAIL_CANNOT_REMOVE_TEST_TAG).assertExists()
    }

    @Test
    fun togglingSendsToggle() {
        compose.setContent {
            TaffyPreview(darkTheme = true) {
                SkillDetailContent(
                    state = builtInState(enabled = false),
                    onIntent = { intents += it },
                )
            }
        }

        compose.onNodeWithTag(SKILL_DETAIL_SWITCH_TEST_TAG).assertIsOff().performClick()
        assertEquals(listOf(SkillDetailIntent.Toggle), intents)
    }

    @Test
    fun aMissingAbilityIsNamedRatherThanInvented() {
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                SkillDetailContent(
                    state = SkillDetailUiState(
                        availability = SkillsRepository.Availability.READY,
                    ),
                    onIntent = { intents += it },
                )
            }
        }

        compose.onNodeWithTag(EMPTY_STATE_TEST_TAG).assertExists()
        compose.onNodeWithTag(SKILL_DETAIL_SWITCH_TEST_TAG).assertDoesNotExist()
    }

    private fun builtInState(enabled: Boolean = true) = SkillDetailUiState(
        availability = SkillsRepository.Availability.READY,
        skill = SkillDetailUiState.Skill(
            id = SkillsRepository.FORM_ASSISTANT,
            enabled = enabled,
            builtIn = true,
            mayUse = listOf(SkillsRepository.MayUse.FORM),
        ),
    )
}

// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import androidx.compose.ui.test.assertHasClickAction
import androidx.compose.ui.test.assertIsOff
import androidx.compose.ui.test.assertIsOn
import androidx.compose.ui.test.hasTestTag
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.performClick
import androidx.compose.ui.test.performScrollToNode
import com.taffygo.browser.ui.core.ui.EMPTY_STATE_TEST_TAG
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyPreview
import org.junit.Assert.assertEquals
import org.junit.Rule
import org.junit.Test

/**
 * Screen SCR-601 — installed abilities with on/off, not a store.
 */
class SkillsListSemanticsTest {

    @get:Rule
    val compose = createComposeRule()

    private val intents = mutableListOf<SkillsListIntent>()

    @Test
    fun aBuiltInRowIsAButtonAndASwitch() {
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                SkillsListContent(state = readyState(), onIntent = { intents += it })
            }
        }

        compose.onNodeWithTag(TaffyDestination.SkillsList.screenId).assertExists()
        compose.onNodeWithTag(SKILLS_NO_EXTRAS_TEST_TAG).assertExists()
        compose.onNodeWithTag("$SKILL_ROW_TEST_TAG_PREFIX${SkillsRepository.FORM_ASSISTANT}")
            .assertExists()
            .assertHasClickAction()
        compose.onNodeWithTag("$SKILL_SWITCH_TEST_TAG_PREFIX${SkillsRepository.FORM_ASSISTANT}")
            .assertExists()
            .assertIsOn()
        compose.onNodeWithTag("$SKILL_SWITCH_TEST_TAG_PREFIX${SkillsRepository.LIBRARY_BUILDER}")
            .assertIsOff()
    }

    @Test
    fun tappingTheRowOpensDetailsAndTheSwitchToggles() {
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                SkillsListContent(state = readyState(), onIntent = { intents += it })
            }
        }

        compose.onNodeWithTag(
            "$SKILL_ROW_TEST_TAG_PREFIX${SkillsRepository.FORM_ASSISTANT}",
        ).performClick()
        compose.onNodeWithTag(
            "$SKILL_SWITCH_TEST_TAG_PREFIX${SkillsRepository.LIBRARY_BUILDER}",
        )
            .performClick()

        assertEquals(
            listOf(
                SkillsListIntent.Open(SkillsRepository.FORM_ASSISTANT),
                SkillsListIntent.Toggle(SkillsRepository.LIBRARY_BUILDER),
            ),
            intents,
        )
    }

    @Test
    fun unavailableIsAnEmptyStateNotAStore() {
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                SkillsListContent(
                    state = SkillsListUiState(
                        availability = SkillsRepository.Availability.UNAVAILABLE,
                    ),
                    onIntent = { intents += it },
                )
            }
        }

        compose.onNodeWithTag(EMPTY_STATE_TEST_TAG).assertExists()
        compose.onNodeWithTag(SKILLS_SEARCH_TEST_TAG).assertDoesNotExist()
        compose.onNodeWithTag(SKILLS_NO_EXTRAS_TEST_TAG).assertDoesNotExist()
    }

    @Test
    fun aFullSiteSkillRosterComposesRowsOnlyAsTheyEnterTheViewport() {
        val rows = List(64) { index ->
            SkillsListUiState.Row(
                id = "site-skill-$index",
                name = "Saved skill $index",
                origin = "example.test",
                stepCount = 2u,
                enabled = true,
            )
        }
        val lastTag = "$SKILL_ROW_TEST_TAG_PREFIX${rows.last().id}"
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                SkillsListContent(
                    state = SkillsListUiState(
                        availability = SkillsRepository.Availability.READY,
                        groups = listOf(
                            SkillsListUiState.Group(SkillsRepository.Group.FORMS, rows),
                        ),
                    ),
                    onIntent = { intents += it },
                )
            }
        }

        compose.onNodeWithTag(lastTag).assertDoesNotExist()
        compose.onNodeWithTag(SKILLS_LIST_TEST_TAG).performScrollToNode(hasTestTag(lastTag))
        compose.onNodeWithTag(lastTag).assertExists()
    }

    private fun readyState() = SkillsListUiState(
        availability = SkillsRepository.Availability.READY,
        showSearch = true,
        showNoExtras = true,
        groups = listOf(
            SkillsListUiState.Group(
                SkillsRepository.Group.FORMS,
                listOf(SkillsListUiState.Row(SkillsRepository.FORM_ASSISTANT, enabled = true)),
            ),
            SkillsListUiState.Group(
                SkillsRepository.Group.LIBRARY,
                listOf(
                    SkillsListUiState.Row(SkillsRepository.LIBRARY_BUILDER, enabled = false),
                ),
            ),
        ),
    )
}

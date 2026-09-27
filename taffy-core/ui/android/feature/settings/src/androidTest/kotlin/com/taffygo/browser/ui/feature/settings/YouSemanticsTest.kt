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
import androidx.compose.ui.test.performScrollTo
import androidx.compose.ui.test.performTextReplacement
import com.taffygo.browser.ui.core.model.LocalAvatar
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyPreview
import org.junit.Assert.assertEquals
import org.junit.Rule
import org.junit.Test

/** Screen SCR-410 — You hub. */
class YouSemanticsTest {

    @get:Rule
    val compose = createComposeRule()

    private val intents = mutableListOf<YouIntent>()

    @Test
    fun everyRowOpensAndTheHeroOpensTheProfile() {
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                YouContent(state = YouUiState(), onIntent = { intents += it })
            }
        }

        compose.onNodeWithTag(TaffyDestination.You.screenId).assertExists()
        compose.onNodeWithTag(YOU_HEADER_TEST_TAG).assertExists().assertHasClickAction()
        YouRow.entries.forEach { row ->
            compose.onNodeWithTag("$YOU_ROW_TEST_TAG_PREFIX${row.name}")
                .assertExists()
                .assertHasClickAction()
        }

        compose.onNodeWithTag(YOU_HEADER_TEST_TAG).performClick()
        compose.onNodeWithTag("$YOU_ROW_TEST_TAG_PREFIX${YouRow.PROFILE.name}").performClick()
        compose.onNodeWithTag("$YOU_ROW_TEST_TAG_PREFIX${YouRow.MEMORY.name}").performClick()
        assertEquals(
            listOf(
                YouIntent.OpenDetails,
                YouIntent.Open(YouRow.PROFILE),
                YouIntent.Open(YouRow.MEMORY),
            ),
            intents,
        )
    }

    /**
     * The details pane holds exactly two things a person can change, and
     * neither of them reaches a server. What it must no longer hold is a
     * sign-in, a sign-out or an identifier to copy, so this asks for the two
     * and nothing more.
     */
    @Test
    fun theDetailsPaneEditsTheNameAndThePicture() {
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                YouContent(
                    state = YouUiState(detailsOpen = true, displayName = "Priya Sharma"),
                    onIntent = { intents += it },
                )
            }
        }

        compose.onNodeWithTag(YOU_DETAILS_SLAB_TEST_TAG).assertExists()
        compose.onNodeWithTag(YOU_NAME_TEST_TAG).performScrollTo()
            .performTextReplacement("Ada L")
        assertEquals(listOf(YouIntent.EditName("Ada L")), intents)

        intents.clear()
        val tile = LocalAvatar.of("a1")
        compose.onNodeWithTag("$YOU_BANNER_TEST_TAG_PREFIX${youAvatarTag(tile)}")
            .performScrollTo()
            .performClick()
        assertEquals(listOf(YouIntent.ChooseAvatar(tile)), intents)
    }
}

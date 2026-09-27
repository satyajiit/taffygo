// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.ui

import androidx.compose.ui.semantics.SemanticsProperties
import androidx.compose.ui.test.SemanticsMatcher
import androidx.compose.ui.test.assert
import androidx.compose.ui.test.assertHasClickAction
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performClick
import org.junit.Assert.assertEquals
import org.junit.Rule
import org.junit.Test

class TaffySetupNeededPanelSemanticsTest {

    @get:Rule
    val compose = createComposeRule()

    @Test
    fun theTitleIsAHeadingAndBothActionsFire() {
        var primary = 0
        var secondary = 0
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                TaffySetupNeededPanel(
                    title = taffyString(R.string.taffy_assistant_ask_taffy),
                    body = taffyString(R.string.taffy_fact_kind_needs_a_new_source),
                    primaryLabel = taffyString(R.string.taffy_assistant_review),
                    onPrimary = { primary++ },
                    secondaryLabel = taffyString(R.string.taffy_action_back),
                    onSecondary = { secondary++ },
                )
            }
        }

        compose.onNodeWithTag(TAFFY_SETUP_NEEDED_PANEL_TEST_TAG).assertExists()
        compose.onNodeWithText("Ask Taffy")
            .assert(SemanticsMatcher.keyIsDefined(SemanticsProperties.Heading))
        compose.onNodeWithText("needs a new source").assertExists()
        compose.onNodeWithTag(TAFFY_SETUP_NEEDED_PRIMARY_TEST_TAG)
            .assertHasClickAction()
            .performClick()
        compose.onNodeWithTag(TAFFY_SETUP_NEEDED_SECONDARY_TEST_TAG)
            .assertHasClickAction()
            .performClick()

        assertEquals(1, primary)
        assertEquals(1, secondary)
    }

    @Test
    fun noWayOutDrawsNoSecondaryAction() {
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                TaffySetupNeededPanel(
                    title = taffyString(R.string.taffy_assistant_ask_taffy),
                    body = taffyString(R.string.taffy_fact_kind_needs_a_new_source),
                    primaryLabel = taffyString(R.string.taffy_assistant_review),
                    onPrimary = {},
                )
            }
        }

        compose.onNodeWithTag(TAFFY_SETUP_NEEDED_PRIMARY_TEST_TAG).assertExists()
        compose.onNodeWithTag(TAFFY_SETUP_NEEDED_SECONDARY_TEST_TAG).assertDoesNotExist()
    }
}

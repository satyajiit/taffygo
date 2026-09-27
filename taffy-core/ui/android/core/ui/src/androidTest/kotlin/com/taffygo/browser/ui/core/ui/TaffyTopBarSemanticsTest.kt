// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.ui

import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.SemanticsProperties
import androidx.compose.ui.test.SemanticsMatcher
import androidx.compose.ui.test.assert
import androidx.compose.ui.test.assertHasClickAction
import androidx.compose.ui.test.assertHeightIsAtLeast
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performClick
import androidx.compose.ui.unit.dp
import org.junit.Assert.assertEquals
import org.junit.Rule
import org.junit.Test

/**
 * Back is its own row; the title is a heading below it.
 */
class TaffyTopBarSemanticsTest {

    @get:Rule
    val compose = createComposeRule()

    @Test
    fun backIsAFortyEightDpControlAboveTheTitle() {
        var clicks = 0
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                TaffyTopBar(
                    title = taffyString(R.string.taffy_mode_you_browse),
                    onBack = { clicks++ },
                )
            }
        }

        compose.onNodeWithTag(BACK_TEST_TAG)
            .assertExists()
            .assertHasClickAction()
            .assertHeightIsAtLeast(48.dp)
            .assert(SemanticsMatcher.expectValue(SemanticsProperties.Role, Role.Button))
            .performClick()

        compose.onNodeWithText("Back").assertExists()
        compose.onNodeWithText("You browse")
            .assertExists()
            .assert(SemanticsMatcher.keyIsDefined(SemanticsProperties.Heading))

        assertEquals(1, clicks)
    }

    @Test
    fun aScreenWithoutBackHasNoBackControl() {
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                TaffyTopBar(title = taffyString(R.string.taffy_mode_you_browse))
            }
        }

        compose.onNodeWithTag(BACK_TEST_TAG).assertDoesNotExist()
        compose.onNodeWithText("You browse").assertExists()
    }
}

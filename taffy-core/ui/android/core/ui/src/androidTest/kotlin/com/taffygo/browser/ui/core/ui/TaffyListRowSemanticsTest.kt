// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.ui

import androidx.compose.material3.Text
import androidx.compose.ui.test.assertContentDescriptionEquals
import androidx.compose.ui.test.assertCountEquals
import androidx.compose.ui.test.assertHasClickAction
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onAllNodesWithText
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.performClick
import org.junit.Assert.assertEquals
import org.junit.Rule
import org.junit.Test

/**
 * One row is one announcement.
 *
 * A row with a title, a supporting line and something on the end is four
 * separate things to look at and one thing to hear. This test pins that: the
 * row is a single accessible element carrying the description the screen wrote,
 * and its parts are not announced again underneath it.
 */
class TaffyListRowSemanticsTest {

    @get:Rule
    val compose = createComposeRule()

    private var clicks = 0

    @Test
    fun theWholeRowIsOneElementWithOneDescription() {
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                TaffyListRow(
                    title = "Retention policy",
                    supporting = "docs.example.test",
                    accessibleDescription = "Retention policy, docs.example.test, 3 facts",
                    testTag = ROW_TAG,
                    onClick = { clicks++ },
                    trailing = { Text(text = "3") },
                )
            }
        }

        compose.onNodeWithTag(ROW_TAG)
            .assertExists()
            .assertContentDescriptionEquals("Retention policy, docs.example.test, 3 facts")
            .assertHasClickAction()
    }

    @Test
    fun theRowIsTappableAsOneTarget() {
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                TaffyListRow(
                    title = "Retention policy",
                    accessibleDescription = "Retention policy",
                    testTag = ROW_TAG,
                    onClick = { clicks++ },
                )
            }
        }

        compose.onNodeWithTag(ROW_TAG).performClick()

        assertEquals(1, clicks)
    }

    @Test
    fun aRowWithNoActionHasNoClickAction() {
        compose.setContent {
            TaffyPreview(darkTheme = true) {
                TaffyListRow(
                    title = "2 conflicts",
                    accessibleDescription = "2 conflicts",
                    testTag = ROW_TAG,
                )
            }
        }

        compose.onNodeWithTag(ROW_TAG).assertExists()
        compose.onAllNodesWithText("2 conflicts").assertCountEquals(1)
    }

    private companion object {
        const val ROW_TAG = "list_row_under_test"
    }
}

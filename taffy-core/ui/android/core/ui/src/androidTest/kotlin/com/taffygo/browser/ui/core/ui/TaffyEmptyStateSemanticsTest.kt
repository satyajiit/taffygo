// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.ui

import androidx.compose.material3.Text
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.onNodeWithText
import org.junit.Rule
import org.junit.Test

/**
 * The illustration slot replaces the glyph well; existing glyph callers still
 * draw a well when no illustration is passed.
 */
class TaffyEmptyStateSemanticsTest {

    @get:Rule
    val compose = createComposeRule()

    @Test
    fun aGlyphCallerStillRendersTitleAndBody() {
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                TaffyEmptyState(
                    title = taffyString(R.string.taffy_mode_you_browse),
                    body = taffyString(R.string.taffy_control_take_over),
                    leading = {
                        Text(text = taffyString(R.string.taffy_control_pause))
                    },
                )
            }
        }

        compose.onNodeWithTag(EMPTY_STATE_TEST_TAG).assertExists()
        compose.onNodeWithText("You browse").assertExists()
        compose.onNodeWithText("Take over").assertExists()
        compose.onNodeWithText("Pause").assertExists()
    }

    @Test
    fun anIllustrationReplacesTheGlyphWell() {
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                TaffyEmptyState(
                    title = taffyString(R.string.taffy_mode_you_browse),
                    body = taffyString(R.string.taffy_control_take_over),
                    leading = {
                        Text(text = taffyString(R.string.taffy_control_pause))
                    },
                    illustration = {
                        Text(text = taffyString(R.string.taffy_assistant_ask_taffy))
                    },
                )
            }
        }

        compose.onNodeWithTag(EMPTY_STATE_TEST_TAG).assertExists()
        compose.onNodeWithText("Ask Taffy").assertExists()
        compose.onNodeWithText("Pause").assertDoesNotExist()
    }
}

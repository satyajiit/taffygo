// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.ui

import androidx.compose.ui.test.assertContentDescriptionEquals
import androidx.compose.ui.test.assertHasClickAction
import androidx.compose.ui.test.assertHasNoClickAction
import androidx.compose.ui.test.assertHeightIsEqualTo
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithContentDescription
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.performClick
import androidx.compose.ui.unit.dp
import org.junit.Assert.assertEquals
import org.junit.Rule
import org.junit.Test

/**
 * A page chip is one announcement plus a named 48 dp remove.
 */
class TaffyPageChipSemanticsTest {

    @get:Rule
    val compose = createComposeRule()

    @Test
    fun theChipMergesTitleAndHostAndNamesRemoveAtFortyEightDp() {
        var removed = 0
        compose.setContent {
            TaffyPreview(darkTheme = false, reducedMotion = true) {
                TaffyPageChip(
                    state = TaffyPageChipState(
                        title = "Sony Bravia listing",
                        host = "croma.com",
                    ),
                    taffyOpenedCaption = "Taffy opened this",
                    closedCaption = "This tab closed",
                    removeContentDescription = REMOVE_NAME,
                    onRemove = { removed++ },
                    testTag = CHIP_TAG,
                )
            }
        }

        compose.onNodeWithTag(CHIP_TAG)
            .assertExists()
            .assertContentDescriptionEquals("Sony Bravia listing, croma.com")

        compose.onNodeWithContentDescription(REMOVE_NAME)
            .assertExists()
            .assertHasClickAction()
            .assertHeightIsEqualTo(48.dp)
            .performClick()

        assertEquals(1, removed)
    }

    @Test
    fun aTaffyOpenedChipHasNoRemove() {
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                TaffyPageChip(
                    state = TaffyPageChipState(
                        title = "Opened listing",
                        host = "croma.com",
                        taffyOpened = true,
                        removable = false,
                    ),
                    taffyOpenedCaption = "Taffy opened this",
                    closedCaption = "This tab closed",
                    removeContentDescription = REMOVE_NAME,
                    testTag = CHIP_TAG,
                )
            }
        }

        compose.onNodeWithTag(CHIP_TAG)
            .assertContentDescriptionEquals("Opened listing, Taffy opened this")
        compose.onNodeWithContentDescription(REMOVE_NAME).assertDoesNotExist()
    }

    @Test
    fun aClosedChipKeepsRemoveAndIsNotTappableToOpen() {
        var opened = 0
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                TaffyPageChip(
                    state = TaffyPageChipState(
                        title = "Closed listing",
                        host = "croma.com",
                        closed = true,
                    ),
                    taffyOpenedCaption = "Taffy opened this",
                    closedCaption = "This tab closed",
                    removeContentDescription = REMOVE_NAME,
                    onRemove = {},
                    onClick = { opened++ },
                    testTag = CHIP_TAG,
                )
            }
        }

        compose.onNodeWithTag(CHIP_TAG)
            .assertContentDescriptionEquals("Closed listing, This tab closed")
            .assertHasNoClickAction()
        compose.onNodeWithContentDescription(REMOVE_NAME).assertHasClickAction()
        assertEquals(0, opened)
    }

    private companion object {
        const val CHIP_TAG = "page_chip_under_test"
        const val REMOVE_NAME = "Remove listing from pages Taffy will look at"
    }
}

// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.ui

import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.size
import androidx.compose.material3.Icon
import androidx.compose.material3.Text
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.test.assertContentDescriptionEquals
import androidx.compose.ui.test.assertHasClickAction
import androidx.compose.ui.test.assertHeightIsAtLeast
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
 * A grouped card's rows merge their parts; a named remove is 48 dp.
 */
class TaffyGroupedCardSemanticsTest {

    @get:Rule
    val compose = createComposeRule()

    @Test
    fun aRowMergesItsPartsAndStaysAtLeastFortyEightDp() {
        var clicks = 0
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                TaffyGroupedCard(testTag = CARD_TAG) {
                    Row(
                        modifier = Modifier
                            .fillMaxWidth()
                            .heightIn(min = 48.dp)
                            .clickable(onClick = { clicks++ })
                            .testTag(ROW_TAG)
                            .semantics(mergeDescendants = true) {
                                contentDescription = ROW_NAME
                            },
                        verticalAlignment = Alignment.CenterVertically,
                    ) {
                        Text(text = "Ad and tracker blocking")
                        Text(text = "On for every site")
                    }
                    TaffyGroupedCardDivider()
                    Row(
                        modifier = Modifier
                            .fillMaxWidth()
                            .heightIn(min = 48.dp)
                            .testTag(REMOVE_ROW_TAG),
                        verticalAlignment = Alignment.CenterVertically,
                    ) {
                        Text(
                            text = "Saved sign-ins",
                            modifier = Modifier.weight(1f),
                        )
                        Box(
                            modifier = Modifier
                                .size(48.dp)
                                .clickable(role = Role.Button, onClick = { clicks++ })
                                .semantics { contentDescription = REMOVE_NAME },
                            contentAlignment = Alignment.Center,
                        ) {
                            Icon(
                                imageVector = TaffyIcon.X,
                                contentDescription = null,
                            )
                        }
                    }
                }
            }
        }

        compose.onNodeWithTag(CARD_TAG).assertExists()
        compose.onNodeWithTag(ROW_TAG)
            .assertExists()
            .assertContentDescriptionEquals(ROW_NAME)
            .assertHasClickAction()
            .assertHeightIsAtLeast(48.dp)
            .performClick()

        compose.onNodeWithContentDescription(REMOVE_NAME)
            .assertExists()
            .assertHasClickAction()
            .assertHeightIsEqualTo(48.dp)

        assertEquals(1, clicks)
    }

    private companion object {
        const val CARD_TAG = "grouped_card_under_test"
        const val ROW_TAG = "grouped_row_under_test"
        const val REMOVE_ROW_TAG = "grouped_remove_row_under_test"
        const val ROW_NAME = "Ad and tracker blocking, On for every site"
        const val REMOVE_NAME = "Remove this page"
    }
}

// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.ui

import androidx.compose.material3.Text
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.ui.test.assertHasClickAction
import androidx.compose.ui.test.assertHeightIsAtLeast
import androidx.compose.ui.test.assertIsOff
import androidx.compose.ui.test.assertIsOn
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithContentDescription
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performClick
import androidx.compose.ui.unit.dp
import androidx.test.platform.app.InstrumentationRegistry
import org.junit.Assert.assertEquals
import org.junit.Rule
import org.junit.Test

/**
 * Shared destination-canvas primitives: one name, one tap, honest values.
 */
class TaffyCanvasSemanticsTest {

    @get:Rule
    val compose = createComposeRule()

    private val context = InstrumentationRegistry.getInstrumentation().targetContext

    @Test
    fun aCategoryTileIsOneTappableTarget() {
        var clicks = 0
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                TaffyCategoryTile(
                    title = taffyString(R.string.taffy_mode_you_browse),
                    summary = taffyString(R.string.taffy_control_take_over),
                    accessibleDescription = taffyString(
                        R.string.taffy_accessible_pair,
                        taffyString(R.string.taffy_mode_you_browse),
                        taffyString(R.string.taffy_control_take_over),
                    ),
                    icon = TaffyIcon.UserCircle,
                    index = taffyChapterIndex(1),
                    onClick = { clicks++ },
                    testTag = TILE_TAG,
                )
            }
        }

        compose.onNodeWithTag(TILE_TAG)
            .assertExists()
            .assertHasClickAction()
            .assertHeightIsAtLeast(48.dp)
            .performClick()

        assertEquals(1, clicks)
    }

    @Test
    fun aChapterTileIsOneHundredAndSixDp() {
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                TaffyCategoryTile(
                    title = taffyString(R.string.taffy_mode_you_browse),
                    accessibleDescription = taffyString(R.string.taffy_mode_you_browse),
                    icon = TaffyIcon.UserCircle,
                    index = taffyChapterIndex(1),
                    chapter = true,
                    onClick = {},
                    testTag = TILE_TAG,
                )
            }
        }

        compose.onNodeWithTag(TILE_TAG)
            .assertExists()
            .assertHasClickAction()
            .assertHeightIsAtLeast(106.dp)
    }

    @Test
    fun aStatTileShowsTheValueItWasGiven() {
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                TaffyStatTile(
                    value = taffyString(R.string.taffy_task_state_done),
                    label = taffyString(R.string.taffy_control_pause),
                    icon = TaffyIcon.ChartBar,
                    testTag = STAT_TAG,
                )
            }
        }

        compose.onNodeWithTag(STAT_TAG).assertExists()
        compose.onNodeWithText("Done").assertExists()
        compose.onNodeWithText("Pause").assertExists()
    }

    @Test
    fun aSwitchTogglesUnderItsOwnName() {
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                val on = remember { mutableStateOf(false) }
                TaffySwitch(
                    checked = on.value,
                    onCheckedChange = { on.value = it },
                    accessibleName = taffyString(R.string.taffy_control_pause),
                    testTag = SWITCH_TAG,
                )
            }
        }

        compose.onNodeWithTag(SWITCH_TAG)
            .assertExists()
            .assertIsOff()
            .performClick()
            .assertIsOn()
    }

    @Test
    fun aHeroCardShowsTheTitleItWasGiven() {
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                TaffyHeroCard(
                    title = taffyString(R.string.taffy_mode_you_browse),
                    eyebrow = taffyString(R.string.taffy_assistant_ask_taffy),
                    body = taffyString(R.string.taffy_control_take_over),
                    testTag = HERO_CARD_TEST_TAG,
                )
            }
        }

        compose.onNodeWithTag(HERO_CARD_TEST_TAG).assertExists()
        compose.onNodeWithText("You browse").assertExists()
    }

    @Test
    fun anInfoTileRecedesWithoutAClick() {
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                TaffyInfoTile(testTag = INFO_TAG) {
                    Text(text = taffyString(R.string.taffy_control_stop))
                }
            }
        }

        compose.onNodeWithTag(INFO_TAG).assertExists()
        compose.onNodeWithText("Stop").assertExists()
    }

    @Test
    fun bentoTileIsOneAccessibleButton() {
        var clicks = 0
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                TaffyBentoTile(
                    title = taffyString(R.string.taffy_mode_you_browse),
                    summary = taffyString(R.string.taffy_control_take_over),
                    accessibleDescription = taffyString(
                        R.string.taffy_accessible_pair,
                        taffyString(R.string.taffy_mode_you_browse),
                        taffyString(R.string.taffy_control_take_over),
                    ),
                    icon = TaffyIcon.UserCircle,
                    wash = TaffyTileWash.RibbonOne,
                    onClick = { clicks++ },
                    testTag = BENTO_TILE_TAG,
                )
            }
        }

        compose.onNodeWithTag(BENTO_TILE_TAG)
            .assertExists()
            .assertHasClickAction()
            .assertHeightIsAtLeast(TaffyBentoTileMinHeight)
            .performClick()
        compose.onNodeWithContentDescription(
            context.getString(
                R.string.taffy_accessible_pair,
                context.getString(R.string.taffy_mode_you_browse),
                context.getString(R.string.taffy_control_take_over),
            ),
        ).assertExists()

        assertEquals(1, clicks)
    }

    @Test
    fun bentoGridDrawsEveryItem() {
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                TaffyBentoGrid(testTag = BENTO_GRID_TAG) {
                    item(span = TaffyBentoSpan.Full) {
                        TaffyStatTile(
                            value = taffyString(R.string.taffy_control_pause),
                            label = taffyString(R.string.taffy_control_pause),
                            testTag = FIRST_TAG,
                        )
                    }
                    item {
                        TaffyStatTile(
                            value = taffyString(R.string.taffy_control_stop),
                            label = taffyString(R.string.taffy_control_stop),
                            testTag = SECOND_TAG,
                        )
                    }
                    item {
                        TaffyBentoTile(
                            title = taffyString(R.string.taffy_control_resume),
                            accessibleDescription = taffyString(R.string.taffy_control_resume),
                            icon = TaffyIcon.Sparkle,
                            testTag = BENTO_TILE_TAG,
                        )
                    }
                }
            }
        }

        compose.onNodeWithTag(BENTO_GRID_TAG).assertExists()
        compose.onNodeWithTag(FIRST_TAG).assertExists()
        compose.onNodeWithTag(SECOND_TAG).assertExists()
        compose.onNodeWithTag(BENTO_TILE_TAG).assertExists()
    }

    @Test
    fun twoUpShowsBothChildren() {
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                TaffyTwoUp(
                    first = {
                        TaffyStatTile(
                            value = taffyString(R.string.taffy_control_pause),
                            label = taffyString(R.string.taffy_control_pause),
                            testTag = FIRST_TAG,
                        )
                    },
                    second = {
                        TaffyStatTile(
                            value = taffyString(R.string.taffy_control_stop),
                            label = taffyString(R.string.taffy_control_stop),
                            testTag = SECOND_TAG,
                        )
                    },
                )
            }
        }

        compose.onNodeWithTag(FIRST_TAG).assertExists()
        compose.onNodeWithTag(SECOND_TAG).assertExists()
    }

    private companion object {
        const val TILE_TAG = "category_tile_under_test"
        const val STAT_TAG = "stat_tile_under_test"
        const val SWITCH_TAG = "switch_under_test"
        const val INFO_TAG = "info_tile_under_test"
        const val FIRST_TAG = "two_up_first"
        const val SECOND_TAG = "two_up_second"
        const val BENTO_TILE_TAG = "bento_tile_under_test"
        const val BENTO_GRID_TAG = "bento_grid_under_test"
    }
}

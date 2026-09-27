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
import androidx.compose.ui.test.assertIsEnabled
import androidx.compose.ui.test.assertIsNotEnabled
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.performClick
import androidx.test.platform.app.InstrumentationRegistry
import com.taffygo.browser.ui.core.model.TaskControl
import org.junit.Assert.assertEquals
import org.junit.Rule
import org.junit.Test

/**
 * Parity row PAR-A11Y-002 as an assertion: every projected control stays
 * reachable in reducer order, ahead of the detail below it.
 */
class TaffyControlBarSemanticsTest {

    @get:Rule
    val compose = createComposeRule()

    private val context = InstrumentationRegistry.getInstrumentation().targetContext
    private val pressed = mutableListOf<TaskControl>()

    @Test
    fun allControlsAreReachableAndClickable() {
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                TaffyControlBar(controls = TaskControl.entries, onControl = { pressed += it })
            }
        }

        TaskControl.entries.forEach { control ->
            compose.onNodeWithTag(controlTestTag(control))
                .assertExists()
                .assertIsEnabled()
                .assertHasClickAction()
        }
    }

    @Test
    fun eachControlSendsItsOwnIntent() {
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                TaffyControlBar(controls = TaskControl.entries, onControl = { pressed += it })
            }
        }

        compose.onNodeWithTag(controlTestTag(TaskControl.TAKE_OVER)).performClick()
        compose.onNodeWithTag(controlTestTag(TaskControl.STOP)).performClick()

        assertEquals(listOf(TaskControl.TAKE_OVER, TaskControl.STOP), pressed)
    }

    @Test
    fun theControlsAreTraversedInTheOrderTheyAreShown() {
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                TaffyControlBar(controls = TaskControl.entries, onControl = { pressed += it })
            }
        }

        TaskControl.entries.forEachIndexed { index, control ->
            compose.onNodeWithTag(controlTestTag(control)).assert(
                SemanticsMatcher.expectValue(
                    SemanticsProperties.TraversalIndex,
                    index.toFloat(),
                ),
            )
        }
    }

    @Test
    fun aDisabledBarSaysSoRatherThanDisappearing() {
        compose.setContent {
            TaffyPreview(darkTheme = true) {
                TaffyControlBar(
                    controls = TaskControl.entries,
                    onControl = { pressed += it },
                    enabled = false,
                )
            }
        }

        TaskControl.entries.forEach { control ->
            compose.onNodeWithTag(controlTestTag(control)).assertExists().assertIsNotEnabled()
        }
    }

    @Test
    fun everyControlCarriesItsOwnWord() {
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                TaffyControlBar(controls = TaskControl.entries, onControl = { pressed += it })
            }
        }

        val words = listOf(
            context.getString(R.string.taffy_control_pause),
            context.getString(R.string.taffy_control_resume),
            context.getString(R.string.taffy_control_stop),
            context.getString(R.string.taffy_control_take_over),
        )
        assertEquals(words.size, words.toSet().size)
    }
}

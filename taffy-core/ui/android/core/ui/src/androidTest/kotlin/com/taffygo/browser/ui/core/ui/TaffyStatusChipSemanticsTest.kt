// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.ui

import androidx.compose.foundation.layout.Column
import androidx.compose.ui.semantics.SemanticsProperties
import androidx.compose.ui.test.SemanticsMatcher
import androidx.compose.ui.test.assertCountEquals
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onAllNodesWithContentDescription
import androidx.compose.ui.test.onAllNodesWithText
import androidx.compose.ui.test.onNodeWithText
import androidx.test.platform.app.InstrumentationRegistry
import com.taffygo.browser.ui.core.model.DownloadState
import com.taffygo.browser.ui.core.model.TaskDisplayState
import org.junit.Rule
import org.junit.Test

/**
 * Parity row PAR-A11Y-004 as an assertion: status is never colour alone.
 *
 * Every status the UI host draws carries a word that is both shown and
 * announced, and a shape that repeats the meaning without being announced
 * twice. This test names each of the seven task states and each download state
 * rather than sampling one, because a status word that went missing in one
 * state is exactly the defect a sample would let through.
 */
class TaffyStatusChipSemanticsTest {

    @get:Rule
    val compose = createComposeRule()

    private val context = InstrumentationRegistry.getInstrumentation().targetContext

    @Test
    fun everyTaskStateShowsItsWordAndAnnouncesIt() {
        compose.setContent {
            TaffyPreview(darkTheme = false, reducedMotion = true) {
                Column {
                    TaskDisplayState.entries.forEach {
                        TaffyStatusChip(presentation = StatusPresentation.of(it))
                    }
                }
            }
        }

        TaskDisplayState.entries.forEach { state ->
            val presentation = StatusPresentation.of(state)
            val word = context.getString(presentation.labelRes)
            compose.onNodeWithText(word).assertExists()
            compose
                .onNode(SemanticsMatcher.expectValue(SemanticsProperties.StateDescription, word))
                .assertExists()
        }
    }

    @Test
    fun theShapeIsDrawnAndNotAnnouncedTwice() {
        compose.setContent {
            TaffyPreview(darkTheme = false, reducedMotion = true) {
                Column {
                    TaskDisplayState.entries.forEach {
                        TaffyStatusChip(presentation = StatusPresentation.of(it))
                    }
                }
            }
        }

        // The glyph carries no content description: the word is announced once,
        // as the chip's state description, and the shape only repeats it.
        TaskDisplayState.entries.forEach { state ->
            val word = context.getString(StatusPresentation.of(state).labelRes)
            compose.onAllNodesWithContentDescription(word).assertCountEquals(0)
        }
    }

    @Test
    fun everyDownloadStateShowsItsWordToo() {
        compose.setContent {
            TaffyPreview(darkTheme = true, reducedMotion = true) {
                Column {
                    DownloadState.entries.forEach {
                        TaffyStatusChip(presentation = StatusPresentation.of(it))
                    }
                }
            }
        }

        DownloadState.entries.forEach { state ->
            val word = context.getString(StatusPresentation.of(state).labelRes)
            compose.onAllNodesWithText(word).assertCountEquals(1)
        }
    }

    @Test
    fun stoppedIsItsOwnWordAndIsNeverTheFailedOne() {
        compose.setContent {
            TaffyPreview(darkTheme = false, reducedMotion = true) {
                Column {
                    TaffyStatusChip(presentation = StatusPresentation.of(TaskDisplayState.STOPPED))
                }
            }
        }

        val stopped = context.getString(
            StatusPresentation.of(TaskDisplayState.STOPPED).labelRes,
        )
        val failed = context.getString(
            StatusPresentation.of(TaskDisplayState.FAILED).labelRes,
        )
        compose.onNodeWithText(stopped).assertExists()
        compose.onAllNodesWithText(failed).assertCountEquals(0)
    }
}

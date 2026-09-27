// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.ui.Modifier
import androidx.compose.ui.test.click
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.performTouchInput
import com.taffygo.browser.ui.core.ui.TaffyPreview
import org.junit.Rule
import org.junit.Test

/**
 * The page Taffy is working says nothing until somebody reaches for it.
 *
 * This used to be a veil and a card standing over the page for the whole of a
 * task, saying what the bottom bar says a row below, and the thing it stood in
 * front of was the only reason to be looking at the page at all. What replaced
 * it is keyed to the touch that needs it, which is a sequence rather than a
 * state — nothing, then something, then nothing again — and a sequence is
 * exactly what a tag assertion at one moment cannot see.
 *
 * **The clock is driven by hand.** `TakeoverInputLock`'s notice is a
 * `withFrameMillis` run, so composition is never idle while it is leaving;
 * under the rule's own auto-advancing clock, `waitForIdle` would run the fade
 * to its end and every assertion about the notice would be made after it had
 * gone. Turning auto-advance off makes each step below a deliberate one.
 */
class TakeoverLockSemanticsTest {

    @get:Rule
    val compose = createComposeRule()

    @Test
    fun theHeldPageIsSilentUntilTouchedAndThenGoesQuietAgain() {
        compose.mainClock.autoAdvance = false
        compose.setContent {
            TaffyPreview(darkTheme = false, reducedMotion = true) {
                Box(modifier = Modifier.fillMaxSize()) {
                    TakeoverInputLock(canTakeOver = true, modifier = Modifier.fillMaxSize())
                }
            }
        }
        compose.mainClock.advanceTimeByFrame()

        // The hold is there from the first frame — it is what stops the page
        // taking touches — and it is the only thing that is.
        compose.onNodeWithTag(TAKEOVER_LOCK_TEST_TAG).assertExists()
        compose.onNodeWithTag(TAKEOVER_NOTICE_TEST_TAG).assertDoesNotExist()

        compose.onNodeWithTag(TAKEOVER_LOCK_TEST_TAG).performTouchInput { click() }
        compose.mainClock.advanceTimeByFrame()
        compose.mainClock.advanceTimeByFrame()

        compose.onNodeWithTag(TAKEOVER_NOTICE_TEST_TAG).assertExists()

        // Past the hold and past the fade, whichever way round they are: the
        // notice leaves on its own, so nothing is left over the page.
        compose.mainClock.advanceTimeBy(NoticeGoneMillis)
        compose.onNodeWithTag(TAKEOVER_NOTICE_TEST_TAG).assertDoesNotExist()
        compose.onNodeWithTag(TAKEOVER_LOCK_TEST_TAG).assertExists()
    }

    private companion object {
        /** Comfortably past the notice's own hold and fade together. */
        const val NoticeGoneMillis = 4_000L
    }
}

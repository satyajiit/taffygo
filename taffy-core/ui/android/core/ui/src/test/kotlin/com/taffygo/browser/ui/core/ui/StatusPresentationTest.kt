// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.ui

import com.taffygo.browser.ui.core.designsystem.TaffyStatusTone
import com.taffygo.browser.ui.core.model.DownloadState
import com.taffygo.browser.ui.core.model.TaskDisplayState
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotEquals
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * Parity row PAR-A11Y-004 at the type level: status is never conveyed by colour
 * alone, and this is the test that says so about every status the UI host
 * shows.
 */
class StatusPresentationTest {

    @Test
    fun `every task state has a word and a shape, not only a tone`() {
        for (state in TaskDisplayState.entries) {
            val presentation = StatusPresentation.of(state)

            assertNotEquals(0, presentation.labelRes)
            // Running's shape is the pulsing dot the chip draws; every other
            // state names its glyph.
            assertTrue(
                state.label,
                presentation.icon != null || presentation.style == TaffyStatusStyle.RUNNING,
            )
        }
    }

    @Test
    fun `every download state has a word and a shape, not only a tone`() {
        for (state in DownloadState.entries) {
            val presentation = StatusPresentation.of(state)

            assertNotEquals(0, presentation.labelRes)
            assertTrue(
                state.label,
                presentation.icon != null || presentation.style == TaffyStatusStyle.RUNNING,
            )
        }
    }

    @Test
    fun `no two task states share a word`() {
        val labels = TaskDisplayState.entries.map { StatusPresentation.of(it).labelRes }

        assertEquals(labels.size, labels.toSet().size)
    }

    @Test
    fun `states that share a tone do not share a shape`() {
        val byTone = TaskDisplayState.entries.groupBy { StatusPresentation.of(it).tone }

        for ((tone, states) in byTone) {
            val icons = states.map { StatusPresentation.of(it).icon }
            assertEquals(tone.name, icons.size, icons.toSet().size)
        }
    }

    @Test
    fun `a failure and a stop are told apart by more than their tone`() {
        val failed = StatusPresentation.of(TaskDisplayState.FAILED)
        val stopped = StatusPresentation.of(TaskDisplayState.STOPPED)

        assertNotEquals(failed.labelRes, stopped.labelRes)
        assertNotEquals(failed.icon, stopped.icon)
        assertNotEquals(failed.style, stopped.style)
        assertEquals(TaffyStatusTone.DANGER, failed.tone)
        assertEquals(TaffyStatusTone.NEUTRAL, stopped.tone)
    }
}

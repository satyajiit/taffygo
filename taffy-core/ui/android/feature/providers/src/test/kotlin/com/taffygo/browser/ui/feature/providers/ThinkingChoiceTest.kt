// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.providers

import com.taffygo.browser.ui.core.model.ThinkingLevel
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * The one rule the thinking control lives or dies by: **Auto is not Off**.
 *
 * Auto is the absence of a preference — Taffy deciding — and `ThinkingLevel`
 * deliberately carries no member for it. Off is a person asking for no thinking
 * phase. A control that folded them together would take a decision away from
 * somebody and quietly change what gets sent, so the difference is asserted
 * here rather than left to the screen.
 */
class ThinkingChoiceTest {

    private val ladder = listOf(
        ThinkingLevel.OFF,
        ThinkingLevel.LOW,
        ThinkingLevel.HIGH,
    )

    @Test
    fun `auto is the absence of a rung and never OFF`() {
        val auto = ThinkingChoice(rungs = ladder, chosen = null)
        val off = ThinkingChoice(rungs = ladder, chosen = ThinkingLevel.OFF)

        // The same ladder, two different standing answers.
        assertNotEquals(auto, off)
        assertNull(auto.chosen)
        assertEquals(ThinkingLevel.OFF, off.chosen)

        // Auto stands only for the absence; Off stands only for the rung.
        assertTrue(auto.automatic)
        assertTrue(auto.stands(null))
        assertFalse(auto.stands(ThinkingLevel.OFF))

        assertFalse(off.automatic)
        assertTrue(off.stands(ThinkingLevel.OFF))
        assertFalse(off.stands(null))
    }

    @Test
    fun `a model offering fewer than two rungs draws no control`() {
        assertFalse(ThinkingChoice(rungs = emptyList()).offered)
        assertFalse(ThinkingChoice(rungs = listOf(ThinkingLevel.MEDIUM)).offered)
        // Two is where a radio group becomes a choice rather than a claim.
        assertTrue(ThinkingChoice(rungs = listOf(ThinkingLevel.OFF, ThinkingLevel.LOW)).offered)
    }

    @Test
    fun `the rungs offered are the model's own, never the enumeration's`() {
        val modest = ThinkingChoice(rungs = listOf(ThinkingLevel.OFF, ThinkingLevel.MEDIUM))

        // XHIGH and MAX are opt-in: a model whose catalog entry does not map
        // them must not be able to be asked for them.
        assertFalse(ThinkingLevel.XHIGH in modest.rungs)
        assertFalse(ThinkingLevel.MAX in modest.rungs)
        assertNotEquals(ThinkingLevel.entries.size, modest.rungs.size)
    }
}

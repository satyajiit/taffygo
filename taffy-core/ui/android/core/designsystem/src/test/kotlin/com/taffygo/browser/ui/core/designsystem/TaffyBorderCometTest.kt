// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.designsystem

import androidx.compose.ui.graphics.Color
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * The travelling light's arithmetic, which is all of it a host can run.
 *
 * `PathMeasure` is an Android graphics object, so whether the arc lands on the
 * outline is device evidence. Where the arc *is* — which is the part that was
 * wrong when this light ran on an angle rather than on the edge — is a
 * function of three floats.
 */
class TaffyBorderCometTest {

    private val perimeter = 100f

    @Test
    fun `a step inside the lap is one span`() {
        val (first, second) = cometArcSpans(from = 20f, to = 30f, perimeter = perimeter)

        assertEquals(20f, first.first, 0.001f)
        assertEquals(30f, first.second, 0.001f)
        assertNull(second)
    }

    @Test
    fun `a step across the start point is two spans that meet there`() {
        val (first, second) = cometArcSpans(from = 95f, to = 105f, perimeter = perimeter)

        assertEquals(95f to 100f, first)
        assertNotNull(second)
        assertEquals(0f to 5f, second)
    }

    @Test
    fun `the tail behind a head at zero is measured from the far end`() {
        // The head runs backwards through zero on every lap. A remainder that
        // answered a negative distance would draw nothing at all, once a lap,
        // which reads as a stutter rather than as a defect.
        val (first, second) = cometArcSpans(from = -10f, to = -4f, perimeter = perimeter)

        assertTrue("a span may never start behind the path", first.first >= 0f)
        assertEquals(90f, first.first, 0.001f)
        assertEquals(96f, first.second, 0.001f)
        assertNull(second)
    }

    @Test
    fun `every step of a full lap is measurable and covers the perimeter once`() {
        val steps = 32
        val step = perimeter / steps
        var covered = 0f

        for (index in 0 until steps) {
            val leading = perimeter - index * step
            val (first, second) = cometArcSpans(leading - step, leading, perimeter)
            for (span in listOfNotNull(first, second)) {
                assertTrue("start is on the path", span.first in 0f..perimeter)
                assertTrue("stop is on the path", span.second in 0f..perimeter)
                assertTrue("a span runs forwards", span.second >= span.first)
                covered += span.second - span.first
            }
        }

        assertEquals(perimeter, covered, 0.01f)
    }

    /**
     * The ramp's ends are its ends.
     *
     * The head of the light is the first hue and the far end of the tail is the
     * last, exactly — an off-by-one in the divisor crops the ramp by however
     * coarse the fade happens to be, and a cropped ramp is not visibly wrong,
     * only quietly not the mark.
     */
    @Test
    fun `a ramp starts on its first stop and ends on its last`() {
        val ramp = listOf(Color.Red, Color.Green, Color.Blue)

        assertEquals(Color.Red, taffyRibbonAt(ramp, 0f))
        assertEquals(Color.Blue, taffyRibbonAt(ramp, 1f))
        assertEquals(Color.Green, taffyRibbonAt(ramp, 0.5f))
    }

    /** A fraction off the end is the end, rather than an index off the list. */
    @Test
    fun `a fraction outside the ramp is clamped to it`() {
        val ramp = listOf(Color.Red, Color.Blue)

        assertEquals(Color.Red, taffyRibbonAt(ramp, -2f))
        assertEquals(Color.Blue, taffyRibbonAt(ramp, 7f))
    }

    /**
     * The two degenerate ramps, which are the ones a caller actually passes.
     *
     * A single-colour list is what a surface on a ground of its own hue passes
     * to get the old one-ink behaviour, and it must not divide by the number of
     * gaps in a ramp that has none.
     */
    @Test
    fun `a one-colour ramp is that colour and an empty one draws nothing`() {
        assertEquals(Color.Red, taffyRibbonAt(listOf(Color.Red), 0f))
        assertEquals(Color.Red, taffyRibbonAt(listOf(Color.Red), 1f))
        assertEquals(Color.Transparent, taffyRibbonAt(emptyList(), 0.5f))
    }

    /** Between two stops it is a blend, not a step. */
    @Test
    fun `a fraction between two stops is between the two colours`() {
        val midway = taffyRibbonAt(listOf(Color.Black, Color.White), 0.5f)

        assertTrue("red is between", midway.red > 0f && midway.red < 1f)
        assertEquals(midway.red, midway.green, 0.0001f)
        assertEquals(midway.red, midway.blue, 0.0001f)
    }
}

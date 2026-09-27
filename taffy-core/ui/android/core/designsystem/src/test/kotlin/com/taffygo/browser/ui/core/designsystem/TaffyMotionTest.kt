// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.designsystem

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * The pure curves behind every animation in the product.
 *
 * They are worth a test of their own because nothing else can catch them. A
 * curve that is subtly wrong still animates: the frames arrive, the value
 * changes, no exception is raised, and the only symptom is that a motion looks
 * slightly off to somebody who cannot say why. The properties below are the
 * ones the theme transition and the loops actually rely on.
 */
class TaffyMotionTest {

    @Test
    fun `every curve starts at nothing and ends at everything`() {
        curves().forEach { (name, curve) ->
            assertEquals(name, 0f, curve(0f), TOLERANCE)
            assertEquals(name, 1f, curve(1f), TOLERANCE)
        }
    }

    @Test
    fun `every curve rises without ever going backwards`() {
        curves().forEach { (name, curve) ->
            var previous = curve(0f)
            samples().forEach { fraction ->
                val value = curve(fraction)
                assertTrue("$name went backwards at $fraction", value >= previous - TOLERANCE)
                previous = value
            }
        }
    }

    @Test
    fun `no curve overshoots its own range`() {
        // A curve that leaves nought-to-one is not a bug on its own — some
        // easings are meant to — but every one of these is read straight into
        // an alpha or a colour blend, where above one and below zero are both
        // meaningless and neither would raise anything.
        curves().forEach { (name, curve) ->
            samples().forEach { fraction ->
                val value = curve(fraction)
                assertTrue("$name went below zero at $fraction", value >= -TOLERANCE)
                assertTrue("$name went above one at $fraction", value <= 1f + TOLERANCE)
            }
        }
    }

    @Test
    fun `a fraction outside the run is held rather than extrapolated`() {
        curves().forEach { (name, curve) ->
            assertEquals(name, 0f, curve(-1f), TOLERANCE)
            assertEquals(name, 1f, curve(2f), TOLERANCE)
        }
    }

    @Test
    fun `ease-in-out is symmetric about its middle`() {
        // Untested until now, and the property the curve is named for: it must
        // accelerate away from nought exactly as it decelerates into one.
        samples().forEach { fraction ->
            assertEquals(
                "asymmetric at $fraction",
                1f - taffyEaseInOut(fraction),
                taffyEaseInOut(1f - fraction),
                TOLERANCE,
            )
        }
        assertEquals(0.5f, taffyEaseInOut(0.5f), TOLERANCE)
    }

    @Test
    fun `ease-out is ahead of linear the whole way`() {
        // What "out" means, and the property the theme transition's rise
        // depends on: most of the distance is covered early, so the body is
        // decelerating into rest rather than still travelling when it stops.
        samples().filter { it > 0f && it < 1f }.forEach { fraction ->
            assertTrue("not ahead at $fraction", taffyEaseOut(fraction) > fraction)
        }
    }

    @Test
    fun `smootherstep leaves and arrives with no acceleration`() {
        // The property that separates it from ease-in-out, and the reason the
        // palette blend reads as calm. Both have zero velocity at the ends;
        // only this one has zero acceleration there too, so the second
        // difference near each end is a whisper rather than a jolt.
        val step = 0.001f
        val startAcceleration = secondDifference(::taffySmootherStep, step, step)
        val endAcceleration = secondDifference(::taffySmootherStep, 1f - 2f * step, step)
        assertTrue("start jolted: $startAcceleration", startAcceleration < 1e-4f)
        assertTrue("end jolted: $endAcceleration", endAcceleration < 1e-4f)

        // Ease-in-out is the contrast: a constant, non-zero second derivative
        // at the same place. If this ever stops holding, the two curves have
        // become the same curve and one of them should go.
        assertTrue(secondDifference(::taffyEaseInOut, step, step) > startAcceleration)
    }

    @Test
    fun `a segment remaps its window onto the whole run`() {
        assertEquals(0f, taffySegment(0.10f, 0.10f, 1f), TOLERANCE)
        assertEquals(1f, taffySegment(1f, 0.10f, 1f), TOLERANCE)
        assertEquals(0.5f, taffySegment(0.55f, 0.10f, 1f), TOLERANCE)
    }

    @Test
    fun `a segment holds at both ends rather than running past them`() {
        assertEquals(0f, taffySegment(0f, 0.10f, 0.86f), TOLERANCE)
        assertEquals(1f, taffySegment(0.99f, 0.10f, 0.86f), TOLERANCE)
    }

    @Test
    fun `an empty or inverted window answers nothing rather than dividing by zero`() {
        // The one failure in this file that would not raise anything. A NaN
        // reaching a Canvas centre draws a blank frame and logs nothing, so
        // the whole transition would silently disappear on one device
        // configuration and nowhere else.
        listOf(
            taffySegment(0.5f, 0.5f, 0.5f),
            taffySegment(0.5f, 0.9f, 0.1f),
            taffySegment(0.5f, 1f, 0f),
        ).forEach { value ->
            assertEquals(0f, value, TOLERANCE)
            assertTrue("produced a NaN", !value.isNaN())
        }
    }

    private fun curves(): List<Pair<String, (Float) -> Float>> = listOf(
        "taffyEaseInOut" to ::taffyEaseInOut,
        "taffyEaseOut" to ::taffyEaseOut,
        "taffySmootherStep" to ::taffySmootherStep,
    )

    private fun samples(): List<Float> = (0..100).map { it / 100f }

    /** How sharply a curve is bending at [at]: the discrete second derivative. */
    private fun secondDifference(curve: (Float) -> Float, at: Float, step: Float): Float {
        val before = curve(at - step)
        val here = curve(at)
        val after = curve(at + step)
        return kotlin.math.abs(after - 2f * here + before)
    }

    private companion object {
        const val TOLERANCE = 1e-4f
    }
}

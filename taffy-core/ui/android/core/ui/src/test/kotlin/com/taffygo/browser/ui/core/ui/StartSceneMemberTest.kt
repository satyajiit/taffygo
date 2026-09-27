// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.ui

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * The whole of the plate choice: a clock, a date, and nothing else.
 *
 * It is a pure function precisely so this test exists — the rule the start page
 * follows can be stated here rather than inferred from a screenshot taken at
 * whatever hour the suite happened to run.
 */
class StartSceneMemberTest {

    @Test
    fun `the day divides at the hours the light changes`() {
        assertEquals(TaffyDayPart.MORNING, StartSceneMember.partFor(5))
        assertEquals(TaffyDayPart.MORNING, StartSceneMember.partFor(11))
        assertEquals(TaffyDayPart.AFTERNOON, StartSceneMember.partFor(12))
        assertEquals(TaffyDayPart.AFTERNOON, StartSceneMember.partFor(16))
        assertEquals(TaffyDayPart.EVENING, StartSceneMember.partFor(17))
        assertEquals(TaffyDayPart.EVENING, StartSceneMember.partFor(20))
        assertEquals(TaffyDayPart.NIGHT, StartSceneMember.partFor(21))
        assertEquals(TaffyDayPart.NIGHT, StartSceneMember.partFor(23))
        assertEquals(TaffyDayPart.NIGHT, StartSceneMember.partFor(0))
        assertEquals(TaffyDayPart.NIGHT, StartSceneMember.partFor(4))
    }

    @Test
    fun `an hour the platform reported oddly costs a picture and not a screen`() {
        assertEquals(TaffyDayPart.NIGHT, StartSceneMember.partFor(-1))
        assertEquals(TaffyDayPart.NIGHT, StartSceneMember.partFor(24))
        // Whatever the hour was, the plate it produced is still one the pack
        // carries — which is the property that matters, not the part.
        assertNotNull(StartSceneMember.pathFor(StartSceneMember.sceneFor(-1, 1)))
        assertNotNull(StartSceneMember.pathFor(StartSceneMember.sceneFor(99, 1)))
    }

    @Test
    fun `every hour of every day of a leap year names a member the pack carries`() {
        val members = StartSceneMember.allMembers().toSet()
        for (day in 1..366) {
            for (hour in 0..23) {
                val path = StartSceneMember.pathFor(StartSceneMember.sceneFor(hour, day))
                assertTrue("day $day hour $hour produced $path", path in members)
            }
        }
    }

    @Test
    fun `the plate holds for a whole part of a day and moves on to the next one`() {
        val morning = StartSceneMember.sceneFor(5, 200)
        assertEquals(morning, StartSceneMember.sceneFor(11, 200))
        assertEquals(morning.variant, StartSceneMember.sceneFor(8, 200).variant)

        // Tomorrow's morning is a different painting, not the same one again.
        assertEquals(TaffyDayPart.MORNING, StartSceneMember.sceneFor(8, 201).part)
        assertTrue(StartSceneMember.sceneFor(8, 201).variant != morning.variant)
    }

    @Test
    fun `the four parts of one day do not all land on the same painting`() {
        val variants = listOf(6, 13, 18, 22)
            .map { StartSceneMember.sceneFor(it, 200).variant }
        assertEquals(variants.size, variants.toSet().size)
    }

    @Test
    fun `a negative day of the year still indexes a member`() {
        val scene = StartSceneMember.sceneFor(8, -2)
        assertTrue(scene.variant in 0 until StartSceneMember.VARIANTS_PER_PART)
        assertNotNull(StartSceneMember.pathFor(scene))
    }

    @Test
    fun `a variant the pack has no member for is refused rather than guessed`() {
        assertNull(StartSceneMember.pathFor(TaffyStartScene(TaffyDayPart.MORNING, -1)))
        assertNull(
            StartSceneMember.pathFor(
                TaffyStartScene(TaffyDayPart.MORNING, StartSceneMember.VARIANTS_PER_PART),
            ),
        )
    }

    @Test
    fun `the pack carries one member for every part and variant, in a stable order`() {
        val members = StartSceneMember.allMembers()
        assertEquals(
            TaffyDayPart.entries.size * StartSceneMember.VARIANTS_PER_PART,
            members.size,
        )
        assertEquals(members.size, members.toSet().size)
        assertEquals("scenes/morning-1.webp", members.first())
        assertEquals("scenes/night-6.webp", members.last())
        assertTrue(members.all { it.matches(Regex("scenes/[a-z0-9._-]+\\.webp")) })
    }
}

// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import com.taffygo.browser.ui.core.model.TabId
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/** How long a tab has been open, as the card's age chip would name it. */
class TabOpenAgeTest {

    @Test
    fun `a missing timestamp has no duration to draw`() {
        assertNull(TabOpenAge.of(0, NOW))
        assertNull(TabOpenAge.of(-1, NOW))
    }

    @Test
    fun `a clock that runs backwards has no duration to draw`() {
        assertNull(TabOpenAge.of(NOW + 1, NOW))
    }

    @Test
    fun `less than a minute is just opened`() {
        val age = TabOpenAge.of(NOW - 30_000, NOW)

        assertTrue(age!!.justOpened)
        assertEquals(0, age.minutes)
    }

    @Test
    fun `minutes under an hour stay minutes`() {
        val age = TabOpenAge.of(NOW - 5 * MINUTE, NOW)

        assertTrue(age!!.underAnHour)
        assertEquals(5, age.minutes)
    }

    @Test
    fun `an hour becomes hours`() {
        val age = TabOpenAge.of(NOW - 90 * MINUTE, NOW)

        assertTrue(age!!.underADay)
        assertEquals(1, age.hours)
    }

    @Test
    fun `a tab that has been nowhere has no duration to draw`() {
        val empty = TabCard(TabId("tab_3"), title = "", host = "", openedAtEpochMillis = NOW - MINUTE)
        val visited = TabCard(TabId("tab_1"), "Retention policy", "docs.example.test")

        assertFalse(TabOpenAge.mayShowOn(empty))
        assertTrue(TabOpenAge.mayShowOn(visited))
    }

    @Test
    fun `a day becomes days`() {
        val age = TabOpenAge.of(NOW - 2 * DAY, NOW)

        assertEquals(2, age!!.days)
    }

    private companion object {
        const val NOW = 1_767_225_600_000L
        const val MINUTE = 60_000L
        const val DAY = 86_400_000L
    }
}

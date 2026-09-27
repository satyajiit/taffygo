// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import com.taffygo.browser.ui.core.browser.FilteringSettings
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Test

/**
 * What screen SCR-206 is told, state by state. The rule with room to get
 * wrong is honesty about the week: a count the seam never published must read
 * as not measured rather than as a number derived from the lifetime total.
 *
 * The projection used to take the delivery plane too, so it could report how
 * much of the block list was on the device. It no longer does: the list is a
 * required part the profile fetches whether or not anybody is looking at this
 * screen, so the row read out a download nobody could act on.
 */
class FilteringSettingsReducerTest {

    @Test
    fun `the browser's configuration is carried through`() {
        val state = projectFilteringSettings(
            FilteringSettings(
                enabled = false,
                exceptionHosts = listOf("news.example.test"),
                blockedTotal = 41,
            ),
        )
        assertFalse(state.enabled)
        assertEquals(listOf("news.example.test"), state.exceptionHosts)
        assertEquals(41L, state.blockedTotal)
        assertNull(state.blockedThisWeek)
    }

    @Test
    fun `a missing week is not invented from the lifetime total`() {
        val state = projectFilteringSettings(
            FilteringSettings(blockedTotal = 12_408),
            BlockingWeekRepository.Snapshot(),
        )
        assertNull(state.blockedThisWeek)
        assertNull(state.minimumSitesThisWeek)
        assertEquals(12_408L, state.blockedTotal)
    }

    @Test
    fun `a measured week of zero is zero`() {
        val state = projectFilteringSettings(
            FilteringSettings(blockedTotal = 12_408),
            BlockingWeekRepository.Snapshot(blockedThisWeek = 0, minimumSitesThisWeek = 0),
        )
        assertEquals(0L, state.blockedThisWeek)
        assertEquals(0, state.minimumSitesThisWeek)
        assertEquals(12_408L, state.blockedTotal)
    }

    @Test
    fun `a week count does not invent a site count`() {
        val state = projectFilteringSettings(
            FilteringSettings(blockedTotal = 12_408),
            BlockingWeekRepository.Snapshot(blockedThisWeek = 84, minimumSitesThisWeek = null),
        )
        assertEquals(84L, state.blockedThisWeek)
        assertNull(state.minimumSitesThisWeek)
        assertEquals(12_408L, state.blockedTotal)
    }

    @Test
    fun `turning blocking off is a SetEnabled intent`() {
        val intent = FilteringSettingsIntent.SetEnabled(false)
        assertEquals(false, intent.enabled)
    }

    @Test
    fun `turning blocking on is a SetEnabled intent`() {
        val intent = FilteringSettingsIntent.SetEnabled(true)
        assertEquals(true, intent.enabled)
    }

    @Test
    fun `a week on the browser snapshot still needs the week port`() {
        val state = projectFilteringSettings(
            FilteringSettings(blockedThisWeek = 84, minimumSitesThisWeek = 3),
            BlockingWeekRepository.Snapshot(),
        )
        assertNull(state.blockedThisWeek)
        assertNull(state.minimumSitesThisWeek)
    }

    @Test
    fun `removing an exception names the host`() {
        val intent = FilteringSettingsIntent.RemoveException("news.example.test")
        assertEquals("news.example.test", intent.host)
    }
}

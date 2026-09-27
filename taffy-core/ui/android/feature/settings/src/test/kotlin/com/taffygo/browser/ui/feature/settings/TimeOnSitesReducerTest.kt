// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotEquals
import org.junit.Assert.assertTrue
import org.junit.Test

/** Screen SCR-411 never invents a bar. */
class TimeOnSitesReducerTest {

    @Test
    fun `unavailable has no sites and no largest bar`() {
        val state = projectTimeOnSites(
            YouFixtures.timeUnavailable,
            TimeOnSitesUiState.Range.TODAY,
        )
        assertEquals(YouSurfaceAvailability.UNAVAILABLE, state.availability)
        assertTrue(state.sites.isEmpty())
        assertEquals(0L, state.largestMillis)
        assertEquals(0f, timeOnSitesBarFraction(48_000L, state.largestMillis), 0f)
    }

    @Test
    fun `loading and unavailable drop any sites the port carried`() {
        val leaked = TimeOnSitesRepository.Snapshot(
            availability = YouSurfaceAvailability.LOADING,
            today = listOf(TimeOnSitesRepository.Site("leaked.test", 60_000)),
            week = listOf(TimeOnSitesRepository.Site("leaked.test", 120_000)),
        )
        val loading = projectTimeOnSites(leaked, TimeOnSitesUiState.Range.TODAY)
        assertEquals(YouSurfaceAvailability.LOADING, loading.availability)
        assertTrue(loading.sites.isEmpty())
        assertEquals(0L, loading.todayMillis)
        assertEquals(0L, loading.weekMillis)

        val unavailable = projectTimeOnSites(
            leaked.copy(availability = YouSurfaceAvailability.UNAVAILABLE),
            TimeOnSitesUiState.Range.THIS_WEEK,
        )
        assertEquals(YouSurfaceAvailability.UNAVAILABLE, unavailable.availability)
        assertTrue(unavailable.sites.isEmpty())
        assertEquals(0L, unavailable.weekMillis)
    }

    @Test
    fun `ready with no sites is empty not unavailable`() {
        val state = projectTimeOnSites(
            TimeOnSitesRepository.Snapshot(availability = YouSurfaceAvailability.READY),
            TimeOnSitesUiState.Range.TODAY,
        )
        assertEquals(YouSurfaceAvailability.READY, state.availability)
        assertTrue(state.sites.isEmpty())
        assertEquals(0L, state.totalMillis)
        assertEquals(0, state.todaySiteCount)
        assertEquals(0, state.weekSiteCount)
    }

    @Test
    fun `zero and negative durations are not rows`() {
        val state = projectTimeOnSites(
            TimeOnSitesRepository.Snapshot(
                availability = YouSurfaceAvailability.READY,
                today = listOf(
                    TimeOnSitesRepository.Site("zero.test", 0L),
                    TimeOnSitesRepository.Site("neg.test", -1L),
                    TimeOnSitesRepository.Site("ok.test", 60_000L),
                ),
            ),
            TimeOnSitesUiState.Range.TODAY,
        )
        assertEquals(listOf("ok.test"), state.sites.map { it.site })
        assertEquals(60_000L, state.todayMillis)
        assertEquals(1, state.todaySiteCount)
    }

    @Test
    fun `bars scale to the largest site in the current range`() {
        val today = projectTimeOnSites(YouFixtures.timeReady, TimeOnSitesUiState.Range.TODAY)
        assertEquals(2, today.sites.size)
        assertEquals("youtube.com", today.sites.first().site)
        assertEquals(today.totalMillis, today.todayMillis)
        assertTrue(today.weekMillis > today.todayMillis)
        assertEquals(1f, timeOnSitesBarFraction(today.largestMillis, today.largestMillis), 0f)
        val smaller = today.sites.last()
        assertTrue(timeOnSitesBarFraction(smaller.durationMillis, today.largestMillis) < 1f)

        val week = projectTimeOnSites(YouFixtures.timeReady, TimeOnSitesUiState.Range.THIS_WEEK)
        assertEquals(YouFixtures.timeReady.week.maxOf { it.durationMillis }, week.largestMillis)
        assertEquals(YouFixtures.timeReady.week.size, week.sites.size)
        assertNotEquals(today.sites, week.sites)
    }

    @Test
    fun `switching range does not invent minutes`() {
        val before = projectTimeOnSites(YouFixtures.timeReady, TimeOnSitesUiState.Range.TODAY)
        val after = reduceTimeOnSites(before, TimeOnSitesIntent.SelectRange(TimeOnSitesUiState.Range.THIS_WEEK))
        assertEquals(TimeOnSitesUiState.Range.THIS_WEEK, after.range)
        assertEquals(before.sites, after.sites)
        assertEquals(before.todayMillis, after.todayMillis)
        assertEquals(before.weekMillis, after.weekMillis)
    }

    @Test
    fun `opening a site is navigation`() {
        val state = projectTimeOnSites(YouFixtures.timeReady, TimeOnSitesUiState.Range.TODAY)
        assertEquals(state, reduceTimeOnSites(state, TimeOnSitesIntent.OpenSite("youtube.com")))
    }

    @Test
    fun `cleared with history is a flag not a duration`() {
        val state = projectTimeOnSites(
            YouFixtures.timeReady.copy(clearedWithHistory = true),
            TimeOnSitesUiState.Range.TODAY,
        )
        assertTrue(state.clearedWithHistory)
        assertEquals(
            YouFixtures.timeReady.today.sumOf { it.durationMillis },
            state.todayMillis,
        )
    }

    @Test
    fun `grouped time remains in totals and stays explicitly grouped`() {
        val snapshot = TimeOnSitesRepository.Snapshot(
            availability = YouSurfaceAvailability.READY,
            today = listOf(TimeOnSitesRepository.Site("", 90_000L, grouped = true)),
            week = listOf(TimeOnSitesRepository.Site("", 90_000L, grouped = true)),
            hasGroupedSites = true,
        )

        val state = projectTimeOnSites(snapshot, TimeOnSitesUiState.Range.TODAY)

        assertEquals(90_000L, state.totalMillis)
        assertTrue(state.sites.single().grouped)
        assertTrue(state.hasGroupedSites)
    }

    @Test
    fun `duration parts never round up a guessed minute`() {
        assertEquals(0L to 0, timeOnSitesParts(0))
        assertEquals(0L to 0, timeOnSitesParts(59_999))
        assertEquals(0L to 1, timeOnSitesParts(60_000))
        assertEquals(2L to 14, timeOnSitesParts((2L * 60 + 14) * 60_000))
        val maximumMinutes = Long.MAX_VALUE / 60_000L
        assertEquals(
            maximumMinutes / 60L to (maximumMinutes % 60L).toInt(),
            timeOnSitesParts(Long.MAX_VALUE),
        )
    }

    @Test
    fun `duration totals saturate instead of wrapping negative`() {
        val state = projectTimeOnSites(
            TimeOnSitesRepository.Snapshot(
                availability = YouSurfaceAvailability.READY,
                today = listOf(
                    TimeOnSitesRepository.Site("long.test", Long.MAX_VALUE),
                    TimeOnSitesRepository.Site("minute.test", 60_000L),
                ),
            ),
            TimeOnSitesUiState.Range.TODAY,
        )

        assertEquals(Long.MAX_VALUE, state.totalMillis)
        assertEquals(Long.MAX_VALUE, state.todayMillis)
        assertEquals(Long.MAX_VALUE, state.largestMillis)
    }
}

// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import com.taffygo.browser.ui.core.browser.FilteringSettings
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.launch
import kotlinx.coroutines.test.runCurrent
import kotlinx.coroutines.test.runTest
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

/**
 * The week port is the week window as the browser seam published it, or the
 * honest absence of one. A lifetime total is never a week.
 */
@OptIn(ExperimentalCoroutinesApi::class)
class BlockingWeekRepositoryTest {

    @Test
    fun `null week fields stay not-counted`() {
        val repo = BrowserBlockingWeekRepository(
            MutableStateFlow(FilteringSettings(blockedTotal = 9)),
        )
        assertNull(repo.snapshot.value.blockedThisWeek)
        assertNull(repo.snapshot.value.minimumSitesThisWeek)
    }

    @Test
    fun `measured week fields pass through`() {
        val repo = BrowserBlockingWeekRepository(
            MutableStateFlow(FilteringSettings(blockedThisWeek = 4, minimumSitesThisWeek = 2)),
        )
        assertEquals(4L, repo.snapshot.value.blockedThisWeek)
        assertEquals(2, repo.snapshot.value.minimumSitesThisWeek)
    }

    @Test
    fun `a zero week is a real zero`() {
        val repo = BrowserBlockingWeekRepository(
            MutableStateFlow(FilteringSettings(blockedThisWeek = 0, minimumSitesThisWeek = 0)),
        )
        assertEquals(0L, repo.snapshot.value.blockedThisWeek)
        assertEquals(0, repo.snapshot.value.minimumSitesThisWeek)
    }

    @Test
    fun `the empty week port is a counted zero`() {
        val repo = EmptyBlockingWeekRepository()
        assertEquals(0L, repo.snapshot.value.blockedThisWeek)
        assertEquals(0, repo.snapshot.value.minimumSitesThisWeek)
    }

    @Test
    fun `the unavailable week port is not a zero`() {
        val repo = UnavailableBlockingWeekRepository()
        assertNull(repo.snapshot.value.blockedThisWeek)
        assertNull(repo.snapshot.value.minimumSitesThisWeek)
    }

    @Test
    fun `a site count is not invented from a week count`() {
        val repo = BrowserBlockingWeekRepository(
            MutableStateFlow(FilteringSettings(blockedThisWeek = 84)),
        )
        assertEquals(84L, repo.snapshot.value.blockedThisWeek)
        assertNull(repo.snapshot.value.minimumSitesThisWeek)
    }

    @Test
    fun `updates follow the filtering flow`() = runTest {
        val filtering = MutableStateFlow(FilteringSettings())
        val repo = BrowserBlockingWeekRepository(filtering)
        val seen = mutableListOf<Long?>()
        val job = launch { repo.snapshot.collect { seen += it.blockedThisWeek } }
        runCurrent()
        filtering.value = FilteringSettings(blockedThisWeek = 4)
        runCurrent()
        job.cancel()
        assertEquals(listOf(null, 4L), seen)
    }
}

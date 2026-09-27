// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.shell

import org.chromium.base.test.BaseRobolectricTestRunner
import org.junit.Assert.assertEquals
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(BaseRobolectricTestRunner::class)
class CoalescedRefreshTest {
    @Test
    fun callbackBurstRefreshesOnceAndCloseWithdrawsPendingWork() {
        val scheduled = mutableListOf<Runnable>()
        val cancelled = mutableListOf<Runnable>()
        var refreshes = 0
        val coalescer = CoalescedRefresh(
            schedule = scheduled::add,
            cancel = cancelled::add,
            refresh = { refreshes += 1 },
        )

        repeat(20) { coalescer.request() }
        assertEquals(1, scheduled.size)
        scheduled.single().run()
        assertEquals(1, refreshes)

        coalescer.request()
        coalescer.close()
        assertEquals(listOf(scheduled.last()), cancelled)
        scheduled.last().run()
        assertEquals(1, refreshes)
    }

    @Test
    fun rejectedScheduleRefreshesSynchronously() {
        var refreshes = 0
        val coalescer = CoalescedRefresh(
            schedule = { false },
            cancel = {},
            refresh = { refreshes += 1 },
        )

        coalescer.request()

        assertEquals(1, refreshes)
    }
}

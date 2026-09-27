// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

/** Screen SCR-412 groups the latest terminal workspace snapshots by day. */
class WhatHappenedReducerTest {
    private val newer = event("newer", 3_000L, 11L)
    private val older = event("older", 2_000L, 10L)
    private val ready = WhatHappenedRepository.Snapshot(
        availability = YouSurfaceAvailability.READY,
        events = listOf(older, newer),
        blockedRequestsThisWeek = 1_204,
    )

    @Test
    fun `unavailable carries no stale workspace rows`() {
        val state = projectWhatHappened(
            WhatHappenedRepository.Snapshot(
                availability = YouSurfaceAvailability.UNAVAILABLE,
                events = listOf(newer),
            ),
        )

        assertTrue(state.days.isEmpty())
        assertEquals(YouSurfaceAvailability.UNAVAILABLE, state.availability)
    }

    @Test
    fun `ready with no terminal workspaces is empty not unavailable`() {
        val state = projectWhatHappened(
            WhatHappenedRepository.Snapshot(availability = YouSurfaceAvailability.READY),
        )

        assertEquals(YouSurfaceAvailability.READY, state.availability)
        assertTrue(state.days.isEmpty())
    }

    @Test
    fun `latest workspace snapshots are newest first`() {
        val state = projectWhatHappened(ready)

        assertEquals(listOf(11L, 10L), state.days.map { it.epochDay })
        assertEquals(listOf("newer"), state.days.first().events.map { it.id })
        assertEquals(1_204L, state.blockedRequestsThisWeek)
    }

    @Test
    fun `duplicate workspace rows keep only the newest deterministic event`() {
        val state = projectWhatHappened(
            ready.copy(events = listOf(older.copy(id = "same"), newer.copy(id = "same"))),
        )

        assertEquals(1, state.days.size)
        assertEquals(3_000L, state.days.single().events.single().epochMillis)
    }

    @Test
    fun `opening a workspace is the only supported intent and does not rewrite facts`() {
        val state = projectWhatHappened(ready)

        assertEquals(state, reduceWhatHappened(state, WhatHappenedIntent.OpenTask("workspace")))
    }

    private fun event(id: String, time: Long, day: Long) = WhatHappenedRepository.Event(
        id = id,
        kind = WhatHappenedRepository.Kind.WORKSPACE_RESULT,
        epochMillis = time,
        epochDay = day,
        sourceCount = 2,
        workspaceId = "workspace-$id",
    )
}

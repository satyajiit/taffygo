// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import java.time.Instant
import java.time.ZoneOffset
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * Screen SCR-201's projection. Empty is not unavailable, a search miss is not
 * empty, and Taffy's working trail never becomes the person's History.
 */
class HistoryReducerTest {

    @Test
    fun `ready with no visits is honest empty, not invented rows`() {
        val state = projectHistory(HistorySnapshot.Ready(emptyList()), "", NOW)

        assertTrue(state.isEmpty)
        assertFalse(state.hasNoMatches)
        assertFalse(state.isUnavailable)
        assertTrue(state.days.isEmpty())
        assertEquals(0, state.totalCount)
        assertTrue(state.canOpen)
    }

    @Test
    fun `unavailable is not empty and cannot open a page`() {
        val state = projectHistory(HistorySnapshot.Unavailable, "", NOW)

        assertTrue(state.isUnavailable)
        assertFalse(state.isEmpty)
        assertFalse(state.canOpen)
        assertTrue(state.days.isEmpty())
    }

    @Test
    fun `loading is a named wait, not an empty list`() {
        val state = projectHistory(HistorySnapshot.Loading, "", NOW)

        assertTrue(state.isLoading)
        assertFalse(state.isEmpty)
        assertFalse(state.isUnavailable)
    }

    @Test
    fun `a search that matches nothing is not empty`() {
        val snapshot = HistorySnapshot.Ready(listOf(visit("hv_1", "Retention policy")))
        val nothingAtAll = projectHistory(HistorySnapshot.Ready(emptyList()), "", NOW)
        val nothingMatching = projectHistory(snapshot, "no such page", NOW)

        assertTrue(nothingAtAll.isEmpty)
        assertFalse(nothingAtAll.hasNoMatches)
        assertFalse(nothingMatching.isEmpty)
        assertTrue(nothingMatching.hasNoMatches)
    }

    @Test
    fun `the query matches title or host`() {
        val snapshot = HistorySnapshot.Ready(
            listOf(
                visit("hv_1", "Retention policy", host = "docs.example.test"),
                visit("hv_2", "Product listing", host = "shop.example.test"),
            ),
        )

        assertEquals(
            listOf("hv_2"),
            projectHistory(snapshot, "SHOP.", NOW).days.single().visits.map { it.id.value },
        )
        assertEquals(
            listOf("hv_1"),
            projectHistory(snapshot, "retention", NOW).days.single().visits.map { it.id.value },
        )
    }

    @Test
    fun `private visits and Taffy's working trail never appear`() {
        val snapshot = HistorySnapshot.Ready(
            listOf(
                visit("hv_person", "Retention policy"),
                visit("hv_private", "Price history", isPrivate = true),
                visit("hv_taffy", "Independent review", isTaffyWorkingTrail = true),
            ),
        )

        val state = projectHistory(snapshot, "", NOW)

        assertEquals(1, state.totalCount)
        assertEquals(listOf("hv_person"), state.days.single().visits.map { it.id.value })
    }

    @Test
    fun `visits group by local day, newest first`() {
        val snapshot = HistorySnapshot.Ready(
            listOf(
                visit("hv_old", "Old", at = NOW - THREE_DAYS),
                visit("hv_today_late", "Late today", at = NOW),
                visit("hv_today_early", "Early today", at = NOW - HOUR),
                visit("hv_yesterday", "Yesterday", at = NOW - DAY),
            ),
        )

        val state = projectHistory(snapshot, "", NOW, ZoneOffset.UTC)

        assertEquals(
            listOf(HistoryDay.Kind.TODAY, HistoryDay.Kind.YESTERDAY, HistoryDay.Kind.DATE),
            state.days.map { it.kind },
        )
        assertEquals(
            listOf("hv_today_late", "hv_today_early"),
            state.days[0].visits.map { it.id.value },
        )
        assertEquals(listOf("hv_yesterday"), state.days[1].visits.map { it.id.value })
        assertEquals(listOf("hv_old"), state.days[2].visits.map { it.id.value })
    }

    @Test
    fun `duplicate visit identities keep the newest record and count it once`() {
        val state = projectHistory(
            HistorySnapshot.Ready(
                listOf(
                    visit("same", "Older", at = NOW - DAY),
                    visit("same", "Newest", at = NOW),
                ),
            ),
            "",
            NOW,
            ZoneOffset.UTC,
        )

        assertEquals(1, state.totalCount)
        assertEquals("Newest", state.days.single().visits.single().title)
    }

    private fun visit(
        id: String,
        title: String,
        host: String = "docs.example.test",
        at: Long = NOW,
        isPrivate: Boolean = false,
        isTaffyWorkingTrail: Boolean = false,
    ) = HistoryVisit(
        id = HistoryVisit.Id(id),
        title = title,
        host = host,
        visitedAtEpochMillis = at,
        isPrivate = isPrivate,
        isTaffyWorkingTrail = isTaffyWorkingTrail,
    )

    private companion object {
        val NOW: Long = Instant.parse("2024-06-15T12:00:00Z").toEpochMilli()
        const val HOUR = 3_600_000L
        const val DAY = 86_400_000L
        const val THREE_DAYS = 3 * DAY
    }
}

// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.test.advanceTimeBy
import kotlinx.coroutines.test.runCurrent
import kotlinx.coroutines.test.runTest
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * Find in page (SCR-106): the engine is asked once typing pauses, only for the
 * newest phrase, and never after the overlay has closed.
 *
 * The controller runs in `backgroundScope`, which is where a view model's own
 * scope stands in a test, so time is moved with [advanceTimeBy] followed by
 * [runCurrent] rather than with `advanceUntilIdle`: that one settles the
 * foreground only and would leave every search this suite starts unrun,
 * passing an assertion about what the engine was asked for by asking it
 * nothing at all.
 */
class FindInPageControllerTest {

    @Test
    fun `a phrase reaches the engine only once the debounce has elapsed`() = runTest {
        val port = RecordingFindInPagePort()
        val controller = FindInPageController(port, backgroundScope)
        controller.open()
        controller.apply(FindInPageIntent.QueryChanged("policy"))
        advanceTimeBy(FindInPageController.QUERY_DEBOUNCE_MILLIS - 1)
        runCurrent()

        assertEquals(emptyList<String>(), port.queries)
        assertEquals("policy", controller.state.value.query)
        assertEquals(0, controller.state.value.matchCount)

        advanceTimeBy(1)
        runCurrent()

        assertEquals(listOf("policy"), port.queries)
        assertEquals(1, controller.state.value.activeIndex)
        assertEquals(3, controller.state.value.matchCount)
    }

    @Test
    fun `a newer phrase supersedes one still waiting, which never reaches the engine`() = runTest {
        val port = RecordingFindInPagePort()
        val controller = FindInPageController(port, backgroundScope)
        controller.open()
        controller.apply(FindInPageIntent.QueryChanged("pol"))
        advanceTimeBy(FindInPageController.QUERY_DEBOUNCE_MILLIS / 2)
        controller.apply(FindInPageIntent.QueryChanged("policy"))
        advanceTimeBy(FindInPageController.QUERY_DEBOUNCE_MILLIS)
        runCurrent()

        assertEquals(listOf("policy"), port.queries)
        assertEquals("policy", controller.state.value.query)
        assertEquals(3, controller.state.value.matchCount)
    }

    @Test
    fun `an answer for a superseded request is discarded`() = runTest {
        // The engine takes a while to answer the first phrase; by the time it
        // does, a newer phrase has been asked for. The stale count must not
        // land on the newer query, whatever order the engine answers in.
        val port = RecordingFindInPagePort(answerDelayMillis = 50L)
        val controller = FindInPageController(port, backgroundScope)
        controller.open()
        controller.apply(FindInPageIntent.QueryChanged("pol"))
        advanceTimeBy(FindInPageController.QUERY_DEBOUNCE_MILLIS + 10)
        assertEquals(listOf("pol"), port.queries)
        controller.apply(FindInPageIntent.Next)
        runCurrent()
        // Past the moment the superseded search would have answered.
        advanceTimeBy(FindInPageController.QUERY_DEBOUNCE_MILLIS)
        runCurrent()

        assertEquals("pol", controller.state.value.query)
        // Only the request that was still current when the engine answered
        // wrote a count: Next, not the superseded search.
        assertEquals(2, controller.state.value.activeIndex)
        assertEquals(3, controller.state.value.matchCount)
    }

    @Test
    fun `closing cancels the waiting search and clears the engine`() = runTest {
        val port = RecordingFindInPagePort()
        val controller = FindInPageController(port, backgroundScope)
        controller.open()
        controller.apply(FindInPageIntent.QueryChanged("policy"))
        controller.close()
        advanceTimeBy(FindInPageController.QUERY_DEBOUNCE_MILLIS * 2)
        runCurrent()

        assertEquals(emptyList<String>(), port.queries)
        assertEquals(1, port.cleared)
        assertFalse(controller.state.value.open)
        assertEquals("", controller.state.value.query)
        assertTrue(controller.state.value.available)
    }

    @Test
    fun `the engine's own count publication updates an open search and nothing else`() = runTest {
        val port = RecordingFindInPagePort()
        val controller = FindInPageController(port, backgroundScope)
        controller.open()
        controller.apply(FindInPageIntent.QueryChanged("policy"))
        advanceTimeBy(FindInPageController.QUERY_DEBOUNCE_MILLIS)
        runCurrent()

        port.matches.value = FindInPagePort.MatchCount(activeIndex = 2, total = 7)
        runCurrent()
        assertEquals(2, controller.state.value.activeIndex)
        assertEquals(7, controller.state.value.matchCount)

        controller.close()
        runCurrent()
        port.matches.value = FindInPagePort.MatchCount(activeIndex = 1, total = 9)
        runCurrent()
        assertEquals(0, controller.state.value.matchCount)
    }
}

/** An engine that remembers what it was asked and answers a fixed count. */
private class RecordingFindInPagePort(
    private val answerDelayMillis: Long = 0L,
) : FindInPagePort {
    val queries = mutableListOf<String>()
    var cleared = 0
    override val isAvailable: Boolean = true
    override val matches = MutableStateFlow(FindInPagePort.MatchCount())

    override suspend fun find(query: String): FindInPagePort.MatchCount {
        queries += query
        if (answerDelayMillis > 0L) kotlinx.coroutines.delay(answerDelayMillis)
        return FindInPagePort.MatchCount(activeIndex = 1, total = 3)
    }

    override suspend fun next(): FindInPagePort.MatchCount =
        FindInPagePort.MatchCount(activeIndex = 2, total = 3)

    override suspend fun previous(): FindInPagePort.MatchCount =
        FindInPagePort.MatchCount(activeIndex = 1, total = 3)

    override suspend fun clear() {
        cleared += 1
    }
}

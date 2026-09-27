// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.shell

import com.taffygo.browser.ui.core.common.AppDispatchers
import com.taffygo.browser.ui.feature.browsing.FindInPagePort
import java.io.Closeable
import kotlin.coroutines.CoroutineContext
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.async
import kotlinx.coroutines.runBlocking
import org.chromium.base.test.BaseRobolectricTestRunner
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(BaseRobolectricTestRunner::class)
class ChromiumFindInPagePortTest {

    @Test
    fun `only a final result from the current generation is published`() = runBlocking {
        val page = FakeTarget()
        val environment = FakeEnvironment(page)
        val port = port(environment)

        val first = async(Dispatchers.Unconfined) { port.find("old") }
        val oldRequest = page.requests.single()
        oldRequest.result(active = 1, total = 8, final = false)
        assertFalse(first.isCompleted)

        val second = async(Dispatchers.Unconfined) { port.find("new") }
        val newRequest = page.requests.last()
        assertEquals(FindInPagePort.MatchCount(), first.await())
        oldRequest.result(active = 7, total = 8, final = true)
        assertEquals(FindInPagePort.MatchCount(), port.matches.value)

        newRequest.result(active = 2, total = 3, final = true)
        assertEquals(FindInPagePort.MatchCount(2, 3), second.await())
        assertEquals(FindInPagePort.MatchCount(2, 3), port.matches.value)
        port.close()
    }

    @Test
    fun `selection and content changes cancel work and clear a final count`() = runBlocking {
        val firstPage = FakeTarget()
        val environment = FakeEnvironment(firstPage)
        val port = port(environment)

        val pending = async(Dispatchers.Unconfined) { port.find("policy") }
        environment.select(FakeTarget())
        assertEquals(FindInPagePort.MatchCount(), pending.await())
        assertTrue(firstPage.requests.single().stopped)
        assertTrue(firstPage.requests.single().closed)

        environment.select(firstPage)
        val completed = async(Dispatchers.Unconfined) { port.find("policy") }
        firstPage.requests.last().result(active = 1, total = 4, final = true)
        assertEquals(FindInPagePort.MatchCount(1, 4), completed.await())
        firstPage.invalidate()
        assertEquals(FindInPagePort.MatchCount(), port.matches.value)
        port.close()
    }

    @Test
    fun `next and previous keep the exact query and direction`() = runBlocking {
        val page = FakeTarget()
        val port = port(FakeEnvironment(page))

        val initial = async(Dispatchers.Unconfined) { port.find("taffy") }
        page.requests.last().result(active = 1, total = 2, final = true)
        initial.await()

        val next = async(Dispatchers.Unconfined) { port.next() }
        assertEquals(Start("taffy", forward = true), page.requests.last().start)
        page.requests.last().result(active = 2, total = 2, final = true)
        assertEquals(FindInPagePort.MatchCount(2, 2), next.await())

        val previous = async(Dispatchers.Unconfined) { port.previous() }
        assertEquals(Start("taffy", forward = false), page.requests.last().start)
        page.requests.last().result(active = 1, total = 2, final = true)
        assertEquals(FindInPagePort.MatchCount(1, 2), previous.await())
        port.close()
    }

    @Test
    fun `bad ordinals and a missing page never invent a selected match`() = runBlocking {
        val page = FakeTarget()
        val environment = FakeEnvironment(page)
        val port = port(environment)
        val answer = async(Dispatchers.Unconfined) { port.find("word") }
        page.requests.single().result(active = 9, total = 2, final = true)

        assertEquals(FindInPagePort.MatchCount(0, 2), answer.await())
        environment.select(null)
        assertFalse(port.isAvailable)
        assertEquals(FindInPagePort.MatchCount(), port.next())
        port.close()
    }

    @Test
    fun `an empty query and a timeout clear the native request`() = runBlocking {
        val page = FakeTarget()
        val environment = FakeEnvironment(page)
        val port = ChromiumFindInPagePort(environment, testDispatchers(), 20L)

        assertEquals(FindInPagePort.MatchCount(), port.find("   "))
        assertTrue(page.requests.isEmpty())
        assertEquals(FindInPagePort.MatchCount(), port.find("never answers"))
        assertTrue(page.requests.single().stopped)
        assertTrue(page.requests.single().closed)
        port.close()
    }

    @Test
    fun `destroy is idempotent and rejects later callbacks`() = runBlocking {
        val page = FakeTarget()
        val port = port(FakeEnvironment(page))
        val answer = async(Dispatchers.Unconfined) { port.find("close") }
        val request = page.requests.single()

        port.destroy()
        port.destroy()
        assertEquals(FindInPagePort.MatchCount(), answer.await())
        request.result(active = 1, total = 1, final = true)
        assertEquals(FindInPagePort.MatchCount(), port.matches.value)
        assertFalse(port.isAvailable)
    }

    @Test
    fun `browser operations use the injected window main dispatcher`() = runBlocking {
        val main = RecordingDispatcher()
        val page = FakeTarget()
        val port = ChromiumFindInPagePort(
            FakeEnvironment(page),
            testDispatchers(main),
            1_000L,
        )

        val answer = async(Dispatchers.Unconfined) { port.find("thread") }
        page.requests.single().result(active = 1, total = 1, final = true)
        assertEquals(FindInPagePort.MatchCount(1, 1), answer.await())
        assertTrue(main.dispatchCount > 0)
        port.close()
    }

    private fun port(environment: FakeEnvironment): ChromiumFindInPagePort =
        ChromiumFindInPagePort(environment, testDispatchers(), 1_000L)

    private fun testDispatchers(
        main: CoroutineDispatcher = Dispatchers.Unconfined,
    ): AppDispatchers = object : AppDispatchers {
        override val main = main
        override val default = Dispatchers.Unconfined
        override val io = Dispatchers.Unconfined
    }

    private class RecordingDispatcher : CoroutineDispatcher() {
        var dispatchCount = 0

        override fun dispatch(context: CoroutineContext, block: Runnable) {
            dispatchCount += 1
            block.run()
        }
    }

    private class FakeEnvironment(
        private var selected: FakeTarget?,
    ) : ChromiumFindInPagePort.Environment {
        private var selectionObserver: (() -> Unit)? = null

        override fun selectedTarget(): ChromiumFindInPagePort.Target? = selected

        override fun observeSelection(onChanged: () -> Unit): Closeable {
            selectionObserver = onChanged
            return Closeable { selectionObserver = null }
        }

        fun select(target: FakeTarget?) {
            selected = target
            selectionObserver?.invoke()
        }
    }

    private class FakeTarget : ChromiumFindInPagePort.Target {
        override val identity: Any = Any()
        val requests = mutableListOf<FakeRequest>()
        private var invalidationObserver: (() -> Unit)? = null

        override fun observeInvalidation(onInvalidated: () -> Unit): Closeable {
            invalidationObserver = onInvalidated
            return Closeable { invalidationObserver = null }
        }

        override fun openRequest(
            onResult: (ChromiumFindInPagePort.Update) -> Unit,
        ): ChromiumFindInPagePort.Request = FakeRequest(onResult).also(requests::add)

        fun invalidate() = invalidationObserver?.invoke()
    }

    private class FakeRequest(
        private val callback: (ChromiumFindInPagePort.Update) -> Unit,
    ) : ChromiumFindInPagePort.Request {
        var start: Start? = null
        var stopped = false
        var closed = false

        override fun start(query: String, forward: Boolean) {
            start = Start(query, forward)
        }

        override fun stop(clearSelection: Boolean) {
            stopped = clearSelection
        }

        override fun close() {
            closed = true
        }

        fun result(active: Int, total: Int, final: Boolean) {
            callback(ChromiumFindInPagePort.Update(active, total, final))
        }
    }

    private data class Start(val query: String, val forward: Boolean)
}

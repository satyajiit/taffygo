// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.app

import com.taffygo.browser.ui.app.PackArtworkCache.Key
import com.taffygo.browser.ui.app.PackArtworkCache.Revision
import com.taffygo.browser.ui.core.model.TaffyPartId
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.async
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.TestScope
import kotlinx.coroutines.test.runCurrent
import kotlinx.coroutines.test.runTest
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

@OptIn(ExperimentalCoroutinesApi::class)
class PackArtworkCacheTest {

    @Test
    fun `the same member and revision share one running decode`() = runTest {
        var current: Revision? = revision("v1")
        val loader = ControlledLoader()
        val cache = cache(currentRevision = { current }, loader = loader::load)
        val key = key(version = "v1", member = "IN")

        val first = async { cache.load(key) }
        runCurrent()
        val second = async { cache.load(key) }
        runCurrent()

        assertEquals(1, loader.calls.size)
        loader.calls.single().result.complete("v1-in")
        runCurrent()

        assertEquals("v1-in", first.await())
        assertEquals("v1-in", second.await())
        assertEquals("v1-in", cache.cached(key))
    }

    @Test
    fun `a superseded revision cannot publish a late completion`() = runTest {
        var current: Revision? = revision("v1")
        val loader = ControlledLoader()
        val cache = cache(currentRevision = { current }, loader = loader::load)
        val oldKey = key(version = "v1", member = "IN")
        val newKey = key(version = "v2", member = "IN")

        val old = async { cache.load(oldKey) }
        runCurrent()
        current = revision("v2")
        val fresh = async { cache.load(newKey) }
        runCurrent()

        assertTrue(old.isCancelled)
        loader.calls[1].result.complete("v2-in")
        runCurrent()
        assertEquals("v2-in", fresh.await())

        assertEquals("v2-in", cache.cached(newKey))

        current = revision("v1")
        assertNull(cache.cached(oldKey))
    }

    @Test
    fun `returning to a revision starts new work and ignores its old cleanup`() = runTest {
        var current: Revision? = revision("v1")
        val loader = ControlledLoader()
        val cache = cache(currentRevision = { current }, loader = loader::load)
        val v1 = key(version = "v1", member = "GB")

        val obsolete = async { cache.load(v1) }
        runCurrent()
        current = revision("v2")
        assertNull(cache.cached(key(version = "v2", member = "GB")))
        current = revision("v1")
        val replacement = async { cache.load(v1) }
        runCurrent()
        assertEquals(2, loader.calls.size)

        assertTrue(obsolete.isCancelled)
        assertFalse(replacement.isCompleted)

        val joined = async { cache.load(v1) }
        runCurrent()
        assertEquals(2, loader.calls.size)

        loader.calls[1].result.complete("replacement-v1")
        runCurrent()
        assertEquals("replacement-v1", replacement.await())
        assertEquals("replacement-v1", joined.await())
    }

    @Test
    fun `a failed decode is forgotten so the next request retries`() = runTest {
        var current: Revision? = revision("v1")
        var calls = 0
        val cache = cache(currentRevision = { current }) {
            calls += 1
            if (calls == 1) null else "recovered"
        }
        val key = key(version = "v1", member = "US")

        assertNull(cache.load(key))
        assertEquals("recovered", cache.load(key))
        assertEquals(2, calls)
    }

    @Test
    fun `an unavailable pack prunes decoded artwork`() = runTest {
        var current: Revision? = revision("v1")
        var calls = 0
        val cache = cache(currentRevision = { current }) {
            calls += 1
            "artwork-$calls"
        }
        val key = key(version = "v1", member = "DE")

        assertEquals("artwork-1", cache.load(key))
        current = null
        assertNull(cache.cached(null))
        current = revision("v1")
        assertEquals("artwork-2", cache.load(key))
        assertEquals(2, calls)
    }

    @Test
    fun `least recently used artwork is evicted at the declared capacity`() = runTest {
        var current: Revision? = revision("v1")
        val calls = mutableMapOf<String, Int>()
        val cache = cache(maxEntries = 2, currentRevision = { current }) { key ->
            val count = calls.getOrDefault(key.member, 0) + 1
            calls[key.member] = count
            "${key.member}-$count"
        }
        val india = key(version = "v1", member = "IN")
        val britain = key(version = "v1", member = "GB")
        val unitedStates = key(version = "v1", member = "US")

        assertEquals("IN-1", cache.load(india))
        assertEquals("GB-1", cache.load(britain))
        assertEquals("IN-1", cache.cached(india))
        assertEquals("US-1", cache.load(unitedStates))
        assertEquals("GB-2", cache.load(britain))

        assertEquals(1, calls["IN"])
        assertEquals(2, calls["GB"])
        assertEquals(1, calls["US"])
    }

    private fun TestScope.cache(
        maxEntries: Int = 64,
        currentRevision: () -> Revision?,
        loader: suspend (Key) -> String?,
    ): PackArtworkCache<String> = PackArtworkCache(
        scope = backgroundScope,
        dispatcher = StandardTestDispatcher(testScheduler),
        maxEntries = maxEntries,
        currentRevision = currentRevision,
        loader = loader,
    )

    private class ControlledLoader {
        val calls = mutableListOf<Call>()

        suspend fun load(key: Key): String? {
            val call = Call(key)
            calls += call
            return call.result.await()
        }
    }

    private data class Call(
        val key: Key,
        val result: CompletableDeferred<String?> = CompletableDeferred(),
    )

    private companion object {
        val PART_ID = TaffyPartId("country-flags")

        fun revision(version: String): Revision = Revision(PART_ID, version)

        fun key(version: String, member: String): Key =
            Key(PART_ID, version, member)
    }
}

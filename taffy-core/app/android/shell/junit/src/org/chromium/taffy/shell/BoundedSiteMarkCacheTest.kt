// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.shell

import org.chromium.base.test.BaseRobolectricTestRunner
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(BaseRobolectricTestRunner::class)
class BoundedSiteMarkCacheTest {
    @Test
    fun `evicts the least recently used mark at its exact bound`() {
        val cache = BoundedSiteMarkCache<String>(2)
        cache.put("one.example", "one")
        cache.put("two.example", "two")

        assertTrue(cache.touch("one.example"))
        cache.put("three.example", "three")

        val snapshot = cache.snapshot()
        assertEquals(mapOf("one.example" to "one", "three.example" to "three"), snapshot)
        assertFalse(cache.touch("two.example"))
    }

    @Test
    fun `replacement and misses never grow the store`() {
        val cache = BoundedSiteMarkCache<String>(1)
        cache.put("one.example", "old")
        assertFalse(cache.touch("absent.example"))
        cache.put("one.example", "new")

        assertEquals(mapOf("one.example" to "new"), cache.snapshot())
        cache.clear()
        assertTrue(cache.snapshot().isEmpty())
    }

    @Test
    fun `a burst publishes one latest snapshot`() {
        val scheduled = ArrayDeque<Runnable>()
        var current = mapOf("one.example" to "one")
        val published = mutableListOf<Map<String, String>>()
        val publisher = CoalescedSiteMarkPublisher(
            snapshot = { current },
            schedule = scheduled::addLast,
            cancel = { task -> scheduled.remove(task) },
        )

        publisher.request(published::add)
        current = current + ("two.example" to "two")
        publisher.request(published::add)

        assertEquals(1, scheduled.size)
        scheduled.removeFirst().run()
        assertEquals(listOf(current), published)
    }

    @Test
    fun `destroy cancels an unpublished snapshot`() {
        val scheduled = ArrayDeque<Runnable>()
        val published = mutableListOf<Map<String, String>>()
        val publisher = CoalescedSiteMarkPublisher(
            snapshot = { mapOf("one.example" to "one") },
            schedule = scheduled::addLast,
            cancel = { task -> scheduled.remove(task) },
        )

        publisher.request(published::add)
        publisher.destroy()

        assertTrue(scheduled.isEmpty())
        assertTrue(published.isEmpty())
    }

    @Test(expected = IllegalArgumentException::class)
    fun `zero capacity is refused`() {
        BoundedSiteMarkCache<String>(0)
    }
}

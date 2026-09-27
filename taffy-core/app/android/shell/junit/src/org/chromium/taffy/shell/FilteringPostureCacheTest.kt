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
class FilteringPostureCacheTest {
    @Test
    fun countOnlyPublicationsReuseTheHostProjection() {
        val cache = FilteringPostureCache()
        var reads = 0
        val reader = {
            reads += 1
            arrayOf("docs.example", "news.example")
        }

        val first = cache.hostsFor(7L, reader)
        val afterCountChange = cache.hostsFor(7L, reader)

        assertEquals(listOf("docs.example", "news.example"), first)
        assertEquals(first, afterCountChange)
        assertEquals(1, reads)
    }

    @Test
    fun aPostureChangeReloadsExactlyOnce() {
        val cache = FilteringPostureCache()
        var hosts = arrayOf("docs.example")
        var reads = 0
        val reader = {
            reads += 1
            hosts
        }
        cache.hostsFor(1L, reader)

        hosts = arrayOf("news.example")
        assertEquals(listOf("news.example"), cache.hostsFor(2L, reader))
        assertEquals(listOf("news.example"), cache.hostsFor(2L, reader))
        assertEquals(2, reads)
    }

    @Test
    fun clearForcesTheNextOwnerToReadItsOwnPosture() {
        val cache = FilteringPostureCache()
        var reads = 0
        val reader = {
            reads += 1
            emptyArray<String>()
        }
        cache.hostsFor(3L, reader)
        cache.clear()
        cache.hostsFor(3L, reader)
        assertEquals(2, reads)
    }
}

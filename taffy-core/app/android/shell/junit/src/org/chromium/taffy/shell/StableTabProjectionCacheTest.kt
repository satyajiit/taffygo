// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.shell

import com.taffygo.browser.ui.core.model.Tab
import com.taffygo.browser.ui.core.model.TabId
import org.chromium.base.test.BaseRobolectricTestRunner
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotSame
import org.junit.Assert.assertSame
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(BaseRobolectricTestRunner::class)
class StableTabProjectionCacheTest {
    @Test
    fun onlyDirtyAndSelectionChangingTabsAreReprojected() {
        val projections = mutableMapOf<Int, Int>()
        val cache = StableTabProjectionCache<Source>(
            idOf = Source::id,
            project = { source, selected ->
                projections[source.id] = projections.getOrDefault(source.id, 0) + 1
                Tab(TabId(source.id.toString()), source.title, "", isSelected = selected)
            },
        )
        val firstSource = Source(1, "First")
        val secondSource = Source(2, "Second")
        val first = cache.snapshot(listOf(firstSource, secondSource), nextSelectedId = 1)

        cache.invalidate(2)
        val second = cache.snapshot(
            listOf(firstSource, secondSource.copy(title = "Changed")),
            nextSelectedId = 1,
        )

        assertSame(first.first(), second.first())
        assertNotSame(first.last(), second.last())
        assertEquals(mapOf(1 to 1, 2 to 2), projections)

        val third = cache.snapshot(
            listOf(firstSource, secondSource.copy(title = "Changed")),
            nextSelectedId = 2,
        )
        assertEquals(mapOf(1 to 2, 2 to 3), projections)
        assertEquals(listOf(false, true), third.map(Tab::isSelected))
    }

    @Test
    fun equalDirtyProjectionReusesThePublishedListAndRemovedIdsLoseTheirCacheEntry() {
        var projections = 0
        val cache = StableTabProjectionCache<Source>(
            idOf = Source::id,
            project = { source, selected ->
                projections += 1
                Tab(TabId(source.id.toString()), source.title, "", isSelected = selected)
            },
        )
        val source = Source(1, "One")
        val first = cache.snapshot(listOf(source), nextSelectedId = 1)

        cache.invalidate(1)
        val equal = cache.snapshot(listOf(source), nextSelectedId = 1)
        assertSame(first, equal)

        cache.snapshot(emptyList(), nextSelectedId = null)
        cache.snapshot(listOf(source), nextSelectedId = 1)
        assertEquals(3, projections)
    }

    private data class Source(val id: Int, val title: String)
}

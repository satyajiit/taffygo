// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.app

import com.taffygo.browser.ui.core.common.AppDispatchers
import com.taffygo.browser.ui.core.common.Clock
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.test.UnconfinedTestDispatcher
import kotlinx.coroutines.test.runTest
import org.junit.Assert.assertEquals
import org.junit.Test

@OptIn(ExperimentalCoroutinesApi::class)
class ProfileFrequentSitesRepositoryTest {

    @Test
    fun `visits are counted per host and ranked most visited first`() = runTest {
        val store = MemoryStore()
        val repository = repository(store)

        repository.recordVisit("docs.taffy.test", "Docs")
        repository.recordVisit("news.taffy.test", "News")
        repository.recordVisit("docs.taffy.test", "Docs again")

        val ranked = repository.sites.value
        assertEquals(listOf("docs.taffy.test", "news.taffy.test"), ranked.map { it.host })
        assertEquals(2L, ranked.first().visitCount)
        assertEquals("Docs again", ranked.first().title)
    }

    @Test
    fun `counts survive a restart through the preference store`() = runTest {
        val store = MemoryStore()
        repository(store).apply {
            recordVisit("docs.taffy.test", "Docs")
            recordVisit("docs.taffy.test", "Docs")
        }

        val restarted = repository(store)

        assertEquals(1, restarted.sites.value.size)
        assertEquals(2L, restarted.sites.value.first().visitCount)
    }

    @Test
    fun `a corrupt record is dropped and the rest of the ranking stands`() = runTest {
        val store = MemoryStore()
        store.putString(
            ProfilePreferenceNames.FREQUENT_SITES,
            "docs.taffy.test\t3\t100\tDocs\n" +
                "not a host\t2\t100\tBad host\n" +
                "news.taffy.test\tnot-a-count\t100\tBad count\n" +
                "mail.taffy.test\t1\t100\tMail",
        )

        val loaded = repository(store).sites.value

        assertEquals(listOf("docs.taffy.test", "mail.taffy.test"), loaded.map { it.host })
    }

    @Test
    fun `titles are scrubbed of separators and a whitespace host is refused`() = runTest {
        val store = MemoryStore()
        val repository = repository(store)

        repository.recordVisit("docs.taffy.test", "A\ttitle\nwith seams")
        repository.recordVisit("not a host", "Ignored")

        assertEquals(1, repository.sites.value.size)
        assertEquals("A title with seams", repository.sites.value.first().title)
    }

    @Test
    fun `the store is bounded and the least visited host is the one pruned`() = runTest {
        val store = MemoryStore()
        val repository = repository(store)
        repeat(50) { index -> repository.recordVisit("site$index.taffy.test", "Site") }
        repeat(50) { index ->
            if (index != 7) repository.recordVisit("site$index.taffy.test", "Site")
        }

        repository.recordVisit("late.taffy.test", "Late")
        repository.recordVisit("late.taffy.test", "Late")

        val hosts = repository.sites.value.map { it.host }
        assertEquals(50, hosts.size)
        assertEquals(false, hosts.contains("site7.taffy.test"))
        assertEquals(true, hosts.contains("late.taffy.test"))
    }

    private fun repository(store: MemoryStore): ProfileFrequentSitesRepository =
        ProfileFrequentSitesRepository(store, OneDispatcher(), TickingClock())

    private class MemoryStore : ProfilePreferenceStore {
        private val strings = mutableMapOf<String, String>()
        private val booleans = mutableMapOf<String, Boolean>()

        override fun getString(name: String): String = strings[name].orEmpty()

        override fun putString(name: String, value: String) {
            strings[name] = value
        }

        override fun getBoolean(name: String): Boolean = booleans[name] ?: false

        override fun putBoolean(name: String, value: Boolean) {
            booleans[name] = value
        }
    }

    private class OneDispatcher(
        dispatcher: CoroutineDispatcher = UnconfinedTestDispatcher(),
    ) : AppDispatchers {
        override val main: CoroutineDispatcher = dispatcher
        override val default: CoroutineDispatcher = dispatcher
        override val io: CoroutineDispatcher = dispatcher
    }

    private class TickingClock : Clock {
        private var now = 0L

        override fun nowEpochMillis(): Long {
            now += 1
            return now
        }
    }
}

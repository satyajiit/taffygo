// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.shell

import com.taffygo.browser.ui.feature.browsing.HistorySnapshot
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.runBlocking
import org.chromium.base.Callback
import org.chromium.base.test.BaseRobolectricTestRunner
import org.chromium.chrome.browser.history.HistoryItem
import org.chromium.chrome.browser.history.HistoryProvider
import org.chromium.url.GURL
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertSame
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.annotation.Config

@RunWith(BaseRobolectricTestRunner::class)
@Config(manifest = Config.NONE)
class ChromiumHistoryRepositoryTest {
    @Test
    fun projectionExcludesPrivateVisitsAndReopensOnlyTheExactLiveItem() = onMain {
        val provider = FakeHistoryProvider()
        val opened = mutableListOf<String>()
        val repository = ChromiumHistoryRepository(provider) { address ->
            opened += address
            true
        }

        provider.answer(
            item("https://www.example.test/path?q=one", "Kept"),
            item("https://actor.example.test/", "Actor", actor = true),
            item("https://blocked.example.test/", "Blocked", blocked = true),
            item("https://person:secret@example.test/private", "Credential"),
            item("file:///private/page", "Local"),
        )
        val ready = repository.snapshot.value as HistorySnapshot.Ready
        assertEquals(listOf("www.example.test"), ready.visits.map { it.host })
        assertEquals(
            listOf("https://www.example.test/path?q=one"),
            ready.visits.map { it.address },
        )

        val id = ready.visits.single().id
        assertTrue(repository.open(id))
        assertEquals(listOf("https://www.example.test/path?q=one"), opened)

        provider.notifyHistoryDeleted()
        provider.answer(item("https://new.example.test/fresh", "New"))
        assertFalse(repository.open(id))
        assertEquals(1, opened.size)
        repository.close()
    }

    @Test
    fun deleteUsesTheItemBoundToTheOpaqueId() = onMain {
        val provider = FakeHistoryProvider()
        val repository = ChromiumHistoryRepository(provider) { true }
        val first = item("https://one.example.test/first", "First")
        val second = item("https://two.example.test/second", "Second")
        provider.answer(first, second)
        val id = (repository.snapshot.value as HistorySnapshot.Ready).visits.first().id

        repository.delete(id)

        assertSame(first, provider.marked.single())
        assertEquals(1, provider.removeCalls)
        repository.close()
    }

    @Test
    fun emptyContinuationsStopAndPublishAfterTheHardPageLimit() = onMain {
        val provider = FakeHistoryProvider()
        val repository = ChromiumHistoryRepository(provider) { true }

        repeat(16) { provider.answer(hasMore = true) }

        assertEquals(15, provider.continuationCalls)
        assertTrue(repository.snapshot.value is HistorySnapshot.Ready)
        repository.close()
    }

    @Test
    fun continuationBoundaryVisitIsProjectedOnce() = onMain {
        val provider = FakeHistoryProvider()
        val repository = ChromiumHistoryRepository(provider) { true }
        val boundary = item("https://boundary.example.test/page", "Boundary")

        provider.answer(boundary, hasMore = true)
        assertTrue(repository.snapshot.value is HistorySnapshot.Loading)
        provider.answer(boundary, item("https://next.example.test/page", "Next"))

        val ready = repository.snapshot.value as HistorySnapshot.Ready
        assertEquals(
            listOf("boundary.example.test", "next.example.test"),
            ready.visits.map { it.host },
        )
        repository.close()
    }

    @Test
    fun destroyUnsubscribesAndIgnoresLateReplies() = onMain {
        val provider = FakeHistoryProvider()
        val repository = ChromiumHistoryRepository(provider) { true }

        repository.close()
        provider.answer(item("https://late.example.test/", "Late"))

        assertEquals(1, provider.destroyCalls)
        assertTrue(repository.snapshot.value is HistorySnapshot.Loading)
    }

    @Test
    fun destroyFailureStillWithdrawsStoredItemAuthority() = onMain {
        val provider = FakeHistoryProvider()
        val repository = ChromiumHistoryRepository(provider) { true }
        provider.answer(item("https://example.test/kept", "Kept"))
        val id = (repository.snapshot.value as HistorySnapshot.Ready).visits.single().id
        provider.failDestroy = true

        runCatching(repository::close)

        assertFalse(repository.open(id))
    }

    private fun item(
        address: String,
        title: String,
        blocked: Boolean = false,
        actor: Boolean = false,
    ): HistoryItem {
        val url = testGurl(address)
        return HistoryItem(
            url,
            url.host,
            title,
            null,
            1_000L,
            longArrayOf(1L),
            blocked,
            actor,
        )
    }

    /** Builds Chromium's serialized URL form without requiring native URL parsing in Robolectric. */
    private fun testGurl(address: String): GURL {
        val schemeLength = address.indexOf(':').also { require(it > 0) }
        val authorityBegin = schemeLength + 3
        val authorityEnd =
            sequenceOf(
                    address.indexOf('/', authorityBegin),
                    address.indexOf('?', authorityBegin),
                    address.indexOf('#', authorityBegin),
                )
                .filter { it >= 0 }
                .minOrNull() ?: address.length
        val userInfoEnd = address.lastIndexOf('@', authorityEnd - 1).takeIf { it >= authorityBegin }
        val userInfoSeparator =
            userInfoEnd?.let { address.indexOf(':', authorityBegin).takeIf { index -> index < it } }
        val hostBegin = userInfoEnd?.plus(1) ?: authorityBegin
        val hostEnd =
            address.indexOf(':', hostBegin).takeIf { it in hostBegin..<authorityEnd } ?: authorityEnd
        val usernameBegin = if (userInfoEnd == null) 0 else authorityBegin
        val usernameLength =
            if (userInfoEnd == null) -1 else (userInfoSeparator ?: userInfoEnd) - authorityBegin
        val passwordBegin = userInfoSeparator?.plus(1) ?: 0
        val passwordLength = userInfoSeparator?.let { userInfoEnd - it - 1 } ?: -1
        val parsed =
            listOf(
                0,
                schemeLength,
                usernameBegin,
                usernameLength,
                passwordBegin,
                passwordLength,
                hostBegin,
                (hostEnd - hostBegin).takeIf { it > 0 } ?: -1,
                0,
                -1,
                0,
                -1,
                0,
                -1,
                0,
                -1,
                false,
                false,
            )
        val body = (listOf(1, true) + parsed + address).joinToString("\u0000")
        return GURL.deserializeLatestVersionOnly("${body.length}\u0000$body")
    }

    private fun onMain(block: suspend () -> Unit) =
        runBlocking(Dispatchers.Main.immediate) { block() }

    private class FakeHistoryProvider : HistoryProvider {
        private lateinit var observer: HistoryProvider.BrowsingHistoryObserver
        val marked = mutableListOf<HistoryItem>()
        var continuationCalls = 0
        var removeCalls = 0
        var destroyCalls = 0
        var failDestroy = false

        fun answer(vararg items: HistoryItem, hasMore: Boolean = false) {
            observer.onQueryHistoryComplete(items.toList(), hasMore)
        }

        fun notifyHistoryDeleted() {
            observer.onHistoryDeleted()
        }

        override fun setObserver(observer: HistoryProvider.BrowsingHistoryObserver) {
            this.observer = observer
        }

        override fun queryHistory(query: String, appId: String?) = Unit
        override fun queryHistoryForHost(hostName: String) = Unit

        override fun queryHistoryContinuation() {
            continuationCalls += 1
        }

        override fun queryApps() = Unit

        override fun getLastVisitToHostBeforeRecentNavigations(
            hostName: String,
            callback: Callback<Long>,
        ) = Unit

        override fun markItemForRemoval(item: HistoryItem) {
            marked += item
        }

        override fun removeItems() {
            removeCalls += 1
        }

        override fun destroy() {
            destroyCalls += 1
            if (failDestroy) error("destroy failed")
        }
    }
}

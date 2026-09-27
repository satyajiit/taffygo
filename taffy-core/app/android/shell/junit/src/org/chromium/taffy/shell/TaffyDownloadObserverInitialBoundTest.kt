// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.shell

import com.taffygo.browser.ui.core.model.DownloadRecord
import org.chromium.base.Callback
import org.chromium.base.test.BaseRobolectricTestRunner
import org.chromium.components.offline_items_collection.ContentId
import org.chromium.components.offline_items_collection.LegacyHelpers
import org.chromium.components.offline_items_collection.OfflineContentProvider
import org.chromium.components.offline_items_collection.OfflineItem
import org.chromium.components.offline_items_collection.OpenParams
import org.chromium.components.offline_items_collection.ShareCallback
import org.chromium.components.offline_items_collection.VisualsCallback
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(BaseRobolectricTestRunner::class)
class TaffyDownloadObserverInitialBoundTest {
    @Test
    fun `too many pre-snapshot removals discard the stale snapshot and retry boundedly`() {
        val provider = InitialProvider()
        val publications = mutableListOf<List<DownloadRecord>>()
        TaffyDownloadObserver(provider, publish = publications::add).start()
        val removed = (0..256).map { index -> downloadId("removed-$index") }
        removed.forEach(provider::remove)

        provider.answerInitial(*removed.map(::item).toTypedArray())

        assertEquals(2, provider.initialRequests)
        assertTrue(publications.isEmpty())
        provider.answerInitial()
        assertEquals(listOf(emptyList<DownloadRecord>()), publications)
    }

    @Test
    fun `continuous removal churn cannot create an unbounded snapshot retry loop`() {
        val provider = InitialProvider()
        val publications = mutableListOf<List<DownloadRecord>>()
        TaffyDownloadObserver(provider, publish = publications::add).start()

        repeat(3) { round ->
            repeat(257) { index -> provider.remove(downloadId("$round-$index")) }
            provider.answerInitial()
        }

        assertEquals(3, provider.initialRequests)
        assertEquals(listOf(emptyList<DownloadRecord>()), publications)
    }

    @Test
    fun `bounded list separately reports whether it covered the whole profile store`() {
        val completeProvider = InitialProvider()
        val complete = mutableListOf<Boolean>()
        TaffyDownloadObserver(
            completeProvider,
            publishCompleteness = complete::add,
            publish = {},
        ).start()
        completeProvider.answerInitial(
            *(0 until 256).map { item(downloadId("complete-$it")) }.toTypedArray(),
        )
        assertTrue(complete.single())

        val boundedProvider = InitialProvider()
        val bounded = mutableListOf<Boolean>()
        TaffyDownloadObserver(
            boundedProvider,
            publishCompleteness = bounded::add,
            publish = {},
        ).start()
        boundedProvider.answerInitial(
            *(0..256).map { item(downloadId("bounded-$it")) }.toTypedArray(),
        )
        assertFalse(bounded.single())
    }

    @Test
    fun `an older live item publishes limited status even when the bounded rows stay equal`() {
        val provider = InitialProvider()
        val completeness = mutableListOf<Boolean>()
        val publications = mutableListOf<List<DownloadRecord>>()
        TaffyDownloadObserver(
            provider,
            publishCompleteness = completeness::add,
            publish = publications::add,
        ).start()
        provider.answerInitial(
            *(0 until 256).map { index ->
                item(downloadId("current-$index")).apply { creationTimeMs = index.toLong() }
            }.toTypedArray(),
        )

        provider.add(item(downloadId("older")).apply { creationTimeMs = -1 })

        assertEquals(listOf(true, false), completeness)
        assertEquals(2, publications.size)
        assertEquals(publications.first(), publications.last())
    }

    @Test
    fun `removing from a limited projection re-reads once to refill and prove completeness`() {
        val provider = InitialProvider()
        val completeness = mutableListOf<Boolean>()
        val publications = mutableListOf<List<DownloadRecord>>()
        TaffyDownloadObserver(
            provider,
            publishCompleteness = completeness::add,
            publish = publications::add,
        ).start()
        val initial = (0..256).map { index ->
            item(downloadId("bounded-$index")).apply { creationTimeMs = index.toLong() }
        }
        provider.answerInitial(*initial.toTypedArray())

        provider.remove(downloadId("bounded-256"))

        assertEquals(2, provider.initialRequests)
        provider.answerInitial(*initial.dropLast(1).toTypedArray())
        assertEquals(true, completeness.last())
        assertEquals(256, publications.last().size)
    }

    private fun item(id: ContentId) = OfflineItem().apply {
        this.id = id
        title = "${id.id}.bin"
        totalSizeBytes = 10
        receivedBytes = 4
    }

    private fun downloadId(id: String) = ContentId(LegacyHelpers.LEGACY_DOWNLOAD_NAMESPACE, id)

    private class InitialProvider : OfflineContentProvider {
        private var observer: OfflineContentProvider.Observer? = null
        private var initial: Callback<ArrayList<OfflineItem>>? = null
        var initialRequests = 0
            private set

        fun remove(id: ContentId) = observer?.onItemRemoved(id) ?: Unit

        fun add(item: OfflineItem) = observer?.onItemsAdded(listOf(item)) ?: Unit

        fun answerInitial(vararg items: OfflineItem) {
            initial?.onResult(arrayListOf(*items))
        }

        override fun getAllItems(callback: Callback<ArrayList<OfflineItem>>) {
            initialRequests++
            initial = callback
        }

        override fun addObserver(observer: OfflineContentProvider.Observer) {
            this.observer = observer
        }

        override fun removeObserver(observer: OfflineContentProvider.Observer) = Unit
        override fun pauseDownload(id: ContentId) = Unit
        override fun resumeDownload(id: ContentId) = Unit
        override fun openItem(openParams: OpenParams, id: ContentId) = Unit
        override fun removeItem(id: ContentId) = Unit
        override fun cancelDownload(id: ContentId) = Unit
        override fun validateDangerousDownload(id: ContentId) = Unit
        override fun getItemById(id: ContentId, callback: Callback<OfflineItem>) = Unit
        override fun getVisualsForItem(id: ContentId, callback: VisualsCallback) = Unit
        override fun getShareInfoForItem(id: ContentId, callback: ShareCallback) = Unit
        override fun renameItem(id: ContentId, name: String, callback: Callback<Int>) = Unit
    }
}

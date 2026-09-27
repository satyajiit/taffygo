// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.shell

import android.net.Uri
import com.taffygo.browser.ui.core.model.DownloadAction
import com.taffygo.browser.ui.core.model.DownloadId
import com.taffygo.browser.ui.core.model.DownloadRecord
import com.taffygo.browser.ui.core.model.DownloadState
import org.chromium.base.Callback
import org.chromium.base.test.BaseRobolectricTestRunner
import org.chromium.components.offline_items_collection.ContentId
import org.chromium.components.offline_items_collection.LegacyHelpers
import org.chromium.components.offline_items_collection.OfflineContentProvider
import org.chromium.components.offline_items_collection.OfflineItem
import org.chromium.components.offline_items_collection.OfflineItemShareInfo
import org.chromium.components.offline_items_collection.OfflineItemState
import org.chromium.components.offline_items_collection.OpenParams
import org.chromium.components.offline_items_collection.ShareCallback
import org.chromium.components.offline_items_collection.VisualsCallback
import org.chromium.url.GURL
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(BaseRobolectricTestRunner::class)
class TaffyDownloadObserverTest {
    @Test
    fun liveChangesOverlayTheOlderInitialSnapshot() {
        val provider = FakeProvider()
        var published = emptyList<DownloadRecord>()
        val readiness = mutableListOf<Boolean>()
        val observer = TaffyDownloadObserver(
            provider = provider,
            publishReadiness = readiness::add,
            publish = { published = it },
        )
        observer.start()

        provider.add(item("new", received = 8))
        assertEquals(listOf(false), readiness)
        provider.remove(downloadId("gone"))
        provider.answerInitial(
            item("new", received = 2),
            item("gone", received = 5),
        )

        assertEquals(listOf("new.bin"), published.map(DownloadRecord::fileName))
        assertEquals(8, published.single().downloadedBytes)
        assertEquals(listOf(false, true), readiness)
    }

    @Test
    fun projectionFiltersPrivateDangerousAndNonDownloadItems() {
        val provider = FakeProvider()
        var published = emptyList<DownloadRecord>()
        val observer = TaffyDownloadObserver(provider) { published = it }
        observer.start()

        provider.answerInitial(
            item("running", state = OfflineItemState.IN_PROGRESS),
            item("paused", state = OfflineItemState.PAUSED),
            item("complete", state = OfflineItemState.COMPLETE),
            item("failed", state = OfflineItemState.INTERRUPTED),
            item("private").apply { isOffTheRecord = true },
            item("dangerous").apply { isDangerous = true },
            item("other").apply { id = ContentId("content_index", "other") },
        )

        assertEquals(
            setOf(
                DownloadState.RUNNING,
                DownloadState.PAUSED,
                DownloadState.COMPLETE,
                DownloadState.FAILED,
            ),
            published.map(DownloadRecord::state).toSet(),
        )
        assertEquals(4, published.size)
        assertFalse(observer.perform(projectedId("private"), DownloadAction.PAUSE))
        assertFalse(observer.perform(projectedId("dangerous"), DownloadAction.PAUSE))
        assertTrue(provider.paused.isEmpty())
    }

    @Test
    fun unchangedOrHiddenLiveItemsDoNotRepublishTheVisibleList() {
        val provider = FakeProvider()
        val publications = mutableListOf<List<DownloadRecord>>()
        TaffyDownloadObserver(provider, publish = publications::add).start()
        provider.answerInitial(item("one", received = 4))

        // OfflineContentProvider may repeat an item when only platform fields
        // outside TaffyGo's projection changed. That must not invalidate the
        // Compose list or make every row redraw.
        provider.add(item("one", received = 4))
        provider.add(item("private").apply { isOffTheRecord = true })
        assertEquals(1, publications.size)

        provider.add(item("one", received = 5))
        assertEquals(2, publications.size)
        assertEquals(5, publications.last().single().downloadedBytes)
    }

    @Test
    fun oneLiveBatchFilesEveryVisibleItemBeforePublishing() {
        val provider = FakeProvider()
        val publications = mutableListOf<List<DownloadRecord>>()
        TaffyDownloadObserver(provider, publish = publications::add).start()
        provider.answerInitial()

        provider.addAll(item("second"), item("first"))

        assertEquals(2, publications.size)
        assertEquals(
            listOf("first.bin", "second.bin"),
            publications.last().map(DownloadRecord::fileName),
        )
    }

    @Test
    fun projectionKeepsOnlyTheNewestContractBound() {
        val provider = FakeProvider()
        var published = emptyList<DownloadRecord>()
        TaffyDownloadObserver(provider) { published = it }.start()

        provider.answerInitial(
            *(0..300).map { index ->
                item("item-$index").apply { creationTimeMs = index.toLong() }
            }.toTypedArray(),
        )

        assertEquals(256, published.size)
        assertEquals("item-300.bin", published.first().fileName)
        assertEquals("item-45.bin", published.last().fileName)
    }

    @Test
    fun progressKeepsBrowserChronologyRatherThanReorderingOpaqueIds() {
        val provider = FakeProvider()
        var published = emptyList<DownloadRecord>()
        TaffyDownloadObserver(provider) { published = it }.start()
        provider.answerInitial(
            item("z-new").apply { creationTimeMs = 200 },
            item("a-old").apply { creationTimeMs = 100 },
        )

        provider.add(item("z-new", received = 9).apply { creationTimeMs = 200 })

        assertEquals(listOf("z-new.bin", "a-old.bin"), published.map(DownloadRecord::fileName))
        assertEquals(9, published.first().downloadedBytes)
    }

    @Test
    fun creationTimeCorrectionReordersAnOtherwiseUnchangedDownload() {
        val provider = FakeProvider()
        val publications = mutableListOf<List<DownloadRecord>>()
        TaffyDownloadObserver(provider, publish = publications::add).start()
        provider.answerInitial(
            item("first").apply { creationTimeMs = 0 },
            item("second").apply { creationTimeMs = 10 },
        )

        provider.add(item("first").apply { creationTimeMs = 20 })

        assertEquals(2, publications.size)
        assertEquals(
            listOf("first.bin", "second.bin"),
            publications.last().map(DownloadRecord::fileName),
        )
    }

    @Test
    fun commandsUseCurrentStateAndResumability() {
        val provider = FakeProvider()
        var published = emptyList<DownloadRecord>()
        val observer = TaffyDownloadObserver(provider) { published = it }
        observer.start()
        provider.answerInitial(item("one", state = OfflineItemState.IN_PROGRESS))
        val id = published.single().id
        assertEquals(
            setOf(DownloadAction.PAUSE, DownloadAction.CANCEL),
            published.single().allowedActions,
        )

        assertTrue(observer.perform(id, DownloadAction.PAUSE))
        assertFalse(observer.perform(id, DownloadAction.RESUME))
        assertTrue(observer.perform(id, DownloadAction.CANCEL))
        assertEquals(listOf(downloadId("one")), provider.paused)
        assertTrue(provider.resumed.isEmpty())
        assertEquals(listOf(downloadId("one")), provider.cancelled)

        provider.add(item("one", state = OfflineItemState.PAUSED, resumable = true))
        assertTrue(observer.perform(id, DownloadAction.RESUME))
        assertEquals(listOf(downloadId("one")), provider.resumed)

        provider.add(item("one", state = OfflineItemState.PAUSED, resumable = false))
        assertFalse(observer.perform(id, DownloadAction.RESUME))
        assertEquals(1, provider.resumed.size)
        assertFalse(observer.perform(DownloadId("not-visible"), DownloadAction.CANCEL))
    }

    @Test
    fun failedDownloadOffersResumeOnlyWhenChromiumSaysItIsResumable() {
        val provider = FakeProvider()
        var published = emptyList<DownloadRecord>()
        val observer = TaffyDownloadObserver(provider) { published = it }
        observer.start()
        provider.answerInitial(item("failed", state = OfflineItemState.INTERRUPTED, resumable = true))
        val id = published.single().id

        assertEquals(
            setOf(DownloadAction.RESUME, DownloadAction.REMOVE),
            published.single().allowedActions,
        )
        assertTrue(observer.perform(id, DownloadAction.RESUME))

        provider.add(item("failed", state = OfflineItemState.INTERRUPTED, resumable = false))
        assertEquals(setOf(DownloadAction.REMOVE), published.single().allowedActions)
        assertFalse(observer.perform(id, DownloadAction.RESUME))
        assertEquals(listOf(downloadId("failed")), provider.resumed)
    }

    @Test
    fun completedActionsRecheckCurrentItemAndShareOnlyContentUris() {
        val provider = FakeProvider()
        var published = emptyList<DownloadRecord>()
        val shares = mutableListOf<Triple<String, String?, Uri>>()
        val observer = TaffyDownloadObserver(
            provider = provider,
            shareFile = { name, mime, uri -> shares += Triple(name, mime, uri) },
            publish = { published = it },
        )
        observer.start()
        provider.answerInitial(item("done", state = OfflineItemState.COMPLETE, openable = true))
        val id = published.single().id

        assertEquals(
            setOf(DownloadAction.OPEN, DownloadAction.SHARE, DownloadAction.REMOVE),
            published.single().allowedActions,
        )
        observer.perform(id, DownloadAction.OPEN)
        observer.perform(id, DownloadAction.SHARE)
        provider.answerShare(downloadId("done"), Uri.parse("file:///private/done.bin"))
        assertTrue(shares.isEmpty())

        observer.perform(id, DownloadAction.SHARE)
        val uri = Uri.parse("content://downloads/done")
        provider.answerShare(downloadId("done"), uri)
        observer.perform(id, DownloadAction.REMOVE)

        assertEquals(listOf(downloadId("done")), provider.opened)
        assertEquals(listOf(downloadId("done")), provider.removed)
        assertEquals(listOf(Triple("done.bin", "application/test", uri)), shares)

        provider.add(item("done", state = OfflineItemState.INTERRUPTED))
        observer.perform(id, DownloadAction.OPEN)
        assertEquals(1, provider.opened.size)
    }

    @Test
    fun destroyUnsubscribesAndIgnoresLateInitialReply() {
        val provider = FakeProvider()
        val publications = mutableListOf<List<DownloadRecord>>()
        val observer = TaffyDownloadObserver(provider, publish = publications::add)
        observer.start()

        observer.destroy()
        provider.answerInitial(item("late"))

        assertEquals(1, provider.removeObserverCalls)
        assertTrue(publications.isEmpty())
    }

    @Test
    fun destroyCancelsInitialTimeoutAndTheDetachedTaskCannotPublish() {
        val provider = FakeProvider()
        val readiness = mutableListOf<Boolean>()
        val publications = mutableListOf<List<DownloadRecord>>()
        var timeout: Runnable? = null
        var cancellations = 0
        val observer = TaffyDownloadObserver(
            provider = provider,
            publishReadiness = readiness::add,
            scheduleInitialTimeout = { timeout = it },
            cancelInitialTimeout = {
                if (timeout === it) {
                    timeout = null
                    cancellations++
                }
            },
            publish = publications::add,
        )
        observer.start()
        val detachedTimeout = timeout ?: error("initial timeout was not scheduled")

        observer.destroy()
        detachedTimeout.run()

        assertEquals(1, cancellations)
        assertTrue(readiness.isEmpty())
        assertTrue(publications.isEmpty())
    }

    @Test
    fun initialProviderFailurePublishesReadyButUnavailableInsteadOfAnEmptySuccess() {
        val provider = FakeProvider().apply { failInitialRead = true }
        val readiness = mutableListOf<Boolean>()
        val completeness = mutableListOf<Boolean>()
        val unavailable = mutableListOf<Boolean>()
        val publications = mutableListOf<List<DownloadRecord>>()

        TaffyDownloadObserver(
            provider = provider,
            publishReadiness = readiness::add,
            publishCompleteness = completeness::add,
            publishUnavailable = unavailable::add,
            publish = publications::add,
        ).start()

        assertEquals(listOf(true), readiness)
        assertEquals(listOf(false), completeness)
        assertEquals(listOf(true), unavailable)
        assertEquals(listOf(emptyList<DownloadRecord>()), publications)
    }

    @Test
    fun stalledInitialReadBecomesUnavailableAndALateReplyStillRecoversWithItsLiveOverlay() {
        val provider = FakeProvider()
        val readiness = mutableListOf<Boolean>()
        val unavailable = mutableListOf<Boolean>()
        val publications = mutableListOf<List<DownloadRecord>>()
        var timeout: Runnable? = null
        val observer = TaffyDownloadObserver(
            provider = provider,
            publishReadiness = readiness::add,
            publishUnavailable = unavailable::add,
            scheduleInitialTimeout = { timeout = it },
            cancelInitialTimeout = { if (timeout === it) timeout = null },
            publish = publications::add,
        )
        observer.start()

        timeout?.run()
        provider.add(item("live", received = 8))
        provider.answerInitial(item("live", received = 2), item("old"))

        assertEquals(listOf(true, true, true), readiness)
        assertEquals(listOf(true, true, false), unavailable)
        assertEquals(listOf("live.bin", "old.bin"), publications.last().map { it.fileName })
        assertEquals(8, publications.last().first { it.fileName == "live.bin" }.downloadedBytes)
    }

    private fun item(
        id: String,
        state: Int = OfflineItemState.IN_PROGRESS,
        received: Long = 4,
        resumable: Boolean = true,
        openable: Boolean = false,
    ) = OfflineItem().apply {
        this.id = downloadId(id)
        title = "$id.bin"
        // The observer's host fallback is not under test here. An empty GURL
        // stays entirely in Java; constructing a parsed URL would turn this
        // host suite into an accidental native-library test.
        url = GURL.emptyGURL()
        this.state = state
        totalSizeBytes = 10
        receivedBytes = received
        isResumable = resumable
        isOpenable = openable
        mimeType = "application/test"
    }

    private fun downloadId(id: String) = ContentId(LegacyHelpers.LEGACY_DOWNLOAD_NAMESPACE, id)

    private fun projectedId(id: String): DownloadId {
        val namespace = LegacyHelpers.LEGACY_DOWNLOAD_NAMESPACE
        return DownloadId("${namespace.length}:$namespace$id")
    }

    private class FakeProvider : OfflineContentProvider {
        private var observer: OfflineContentProvider.Observer? = null
        private var initial: Callback<ArrayList<OfflineItem>>? = null
        val paused = mutableListOf<ContentId>()
        val resumed = mutableListOf<ContentId>()
        val cancelled = mutableListOf<ContentId>()
        val opened = mutableListOf<ContentId>()
        val removed = mutableListOf<ContentId>()
        private val pendingShares = ArrayDeque<Pair<ContentId, ShareCallback>>()
        var removeObserverCalls = 0
        var failInitialRead = false

        fun answerInitial(vararg items: OfflineItem) {
            initial?.onResult(arrayListOf(*items))
        }

        fun add(item: OfflineItem) {
            observer?.onItemsAdded(listOf(item))
        }

        fun addAll(vararg items: OfflineItem) {
            observer?.onItemsAdded(items.toList())
        }

        fun remove(id: ContentId) {
            observer?.onItemRemoved(id)
        }

        fun answerShare(id: ContentId, uri: Uri) {
            val (requestedId, callback) = pendingShares.removeFirst()
            assertEquals(requestedId, id)
            callback.onShareInfoAvailable(
                ContentId(id.namespace, id.id),
                OfflineItemShareInfo().apply { this.uri = uri },
            )
        }

        override fun getAllItems(callback: Callback<ArrayList<OfflineItem>>) {
            if (failInitialRead) throw IllegalStateException("profile store unavailable")
            initial = callback
        }

        override fun addObserver(observer: OfflineContentProvider.Observer) {
            this.observer = observer
        }

        override fun removeObserver(observer: OfflineContentProvider.Observer) {
            if (this.observer === observer) this.observer = null
            removeObserverCalls += 1
        }

        override fun pauseDownload(id: ContentId) {
            paused += ContentId(id.namespace, id.id)
        }

        override fun resumeDownload(id: ContentId) {
            resumed += ContentId(id.namespace, id.id)
        }

        override fun openItem(openParams: OpenParams, id: ContentId) {
            opened += ContentId(id.namespace, id.id)
        }
        override fun removeItem(id: ContentId) {
            removed += ContentId(id.namespace, id.id)
        }
        override fun cancelDownload(id: ContentId) {
            cancelled += ContentId(id.namespace, id.id)
        }
        override fun validateDangerousDownload(id: ContentId) = Unit
        override fun getItemById(id: ContentId, callback: Callback<OfflineItem>) = Unit
        override fun getVisualsForItem(id: ContentId, callback: VisualsCallback) = Unit
        override fun getShareInfoForItem(id: ContentId, callback: ShareCallback) {
            pendingShares += ContentId(id.namespace, id.id) to callback
        }
        override fun renameItem(id: ContentId, name: String, callback: Callback<Int>) = Unit
    }
}

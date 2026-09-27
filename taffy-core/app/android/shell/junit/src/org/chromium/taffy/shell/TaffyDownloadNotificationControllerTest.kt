// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.shell

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
import org.chromium.components.offline_items_collection.OfflineItemState
import org.chromium.components.offline_items_collection.OpenParams
import org.chromium.components.offline_items_collection.ShareCallback
import org.chromium.components.offline_items_collection.VisualsCallback
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(BaseRobolectricTestRunner::class)
class TaffyDownloadNotificationControllerTest {
    private val controllers = mutableListOf<TaffyDownloadNotificationController>()

    @After
    fun closeControllers() {
        controllers.asReversed().forEach(TaffyDownloadNotificationController::close)
        controllers.clear()
    }

    @Test
    fun controlWaitsForInitialSnapshotThenUsesTheLiveProviderItem() {
        val provider = FakeProvider()
        val sink = FakeSink()
        val profileToken = "a".repeat(43)
        controller(provider, profileToken, sink)
        val request = request(profileToken, DownloadAction.PAUSE)

        assertTrue(TaffyDownloadActionRegistry.dispatch(request))
        assertTrue(provider.paused.isEmpty())
        provider.answerInitial(item("one", OfflineItemState.IN_PROGRESS))

        assertEquals(listOf(downloadId("one")), provider.paused)
        assertEquals(listOf("one.bin"), sink.shown.map { it.second.fileName })
        assertEquals(setOf(projectedId("one")), sink.reconciled.single().second)
    }

    @Test
    fun receiverRouteCannotPerformAControlTheCurrentSnapshotDoesNotAllow() {
        val provider = FakeProvider()
        val sink = FakeSink()
        val profileToken = "b".repeat(43)
        controller(provider, profileToken, sink)
        provider.answerInitial(item("one", OfflineItemState.PAUSED, resumable = false))

        assertFalse(
            TaffyDownloadActionRegistry.dispatch(request(profileToken, DownloadAction.RESUME)),
        )
        assertFalse(TaffyDownloadActionRegistry.dispatch(request(profileToken, DownloadAction.OPEN)))
        assertTrue(provider.resumed.isEmpty())
    }

    @Test
    fun resumeIsAcceptedOnlyAfterTheExactLiveItemAdvertisesIt() {
        val provider = FakeProvider()
        val profileToken = "f".repeat(43)
        controller(provider, profileToken, FakeSink())
        provider.answerInitial(item("one", OfflineItemState.IN_PROGRESS))

        assertFalse(
            TaffyDownloadActionRegistry.dispatch(request(profileToken, DownloadAction.RESUME)),
        )
        provider.add(item("one", OfflineItemState.PAUSED, resumable = true))
        assertTrue(
            TaffyDownloadActionRegistry.dispatch(request(profileToken, DownloadAction.RESUME)),
        )
        assertEquals(listOf(downloadId("one")), provider.resumed)
        assertFalse(
            TaffyDownloadActionRegistry.dispatch(request(profileToken, DownloadAction.PAUSE)),
        )
    }

    @Test
    fun historicalTerminalIsSilentButALiveTerminalTransitionIsPostedOnce() {
        val provider = FakeProvider()
        val sink = FakeSink()
        val controller = controller(provider, "c".repeat(43), sink)
        provider.answerInitial(item("one", OfflineItemState.COMPLETE))
        assertTrue(sink.shown.isEmpty())

        provider.add(item("one", OfflineItemState.IN_PROGRESS))
        provider.add(item("one", OfflineItemState.COMPLETE))

        assertEquals(
            listOf(DownloadState.RUNNING, DownloadState.COMPLETE),
            sink.shown.map { (_, record) -> record.state },
        )
        controller.close()
        assertEquals(1, provider.removeObserverCalls)
        assertEquals(listOf("c".repeat(43)), sink.cancelledProfiles)
        assertFalse(
            TaffyDownloadActionRegistry.dispatch(request("c".repeat(43), DownloadAction.CANCEL)),
        )
    }

    @Test
    fun failedOrAbsentProviderNeverTurnsAQueuedControlIntoAuthority() {
        val sink = FakeSink()
        val profileToken = "d".repeat(43)
        controller(null, profileToken, sink)

        assertFalse(
            TaffyDownloadActionRegistry.dispatch(request(profileToken, DownloadAction.CANCEL)),
        )
        assertTrue(sink.shown.isEmpty())
    }

    @Test
    fun withdrawnNativeProviderFailsTheControlClosed() {
        val provider = FakeProvider()
        val profileToken = "e".repeat(43)
        controller(provider, profileToken, FakeSink())
        provider.answerInitial(item("one", OfflineItemState.IN_PROGRESS))
        provider.failPause = true

        assertFalse(
            TaffyDownloadActionRegistry.dispatch(request(profileToken, DownloadAction.PAUSE)),
        )
        assertTrue(provider.paused.isEmpty())
    }

    private fun controller(
        provider: OfflineContentProvider?,
        profileToken: String,
        sink: FakeSink,
    ) = TaffyDownloadNotificationController(provider, profileToken, sink).also(controllers::add)

    private fun request(profileToken: String, action: DownloadAction) =
        TaffyDownloadControlRequest(
            profileToken = profileToken,
            downloadId = projectedId("one"),
            action = action,
        )

    private fun item(id: String, state: Int, resumable: Boolean = true) = OfflineItem().apply {
        this.id = downloadId(id)
        title = "$id.bin"
        this.state = state
        totalSizeBytes = 10
        receivedBytes = if (state == OfflineItemState.COMPLETE) 10 else 4
        isResumable = resumable
    }

    private fun downloadId(id: String) = ContentId(LegacyHelpers.LEGACY_DOWNLOAD_NAMESPACE, id)

    private fun projectedId(id: String): DownloadId {
        val namespace = LegacyHelpers.LEGACY_DOWNLOAD_NAMESPACE
        return DownloadId("${namespace.length}:$namespace$id")
    }

    private class FakeSink : TaffyDownloadNotificationSink {
        val shown = mutableListOf<Pair<String, DownloadRecord>>()
        val reconciled = mutableListOf<Pair<String, Set<DownloadId>>>()
        val cancelledProfiles = mutableListOf<String>()

        override fun show(profileToken: String, record: DownloadRecord): Boolean {
            shown += profileToken to record
            return true
        }

        override fun cancel(profileToken: String, id: DownloadId) = Unit

        override fun reconcile(profileToken: String, retainedIds: Set<DownloadId>) {
            reconciled += profileToken to retainedIds
        }

        override fun cancelProfile(profileToken: String) {
            cancelledProfiles += profileToken
        }
    }

    private class FakeProvider : OfflineContentProvider {
        private var observer: OfflineContentProvider.Observer? = null
        private var initial: Callback<ArrayList<OfflineItem>>? = null
        val paused = mutableListOf<ContentId>()
        val resumed = mutableListOf<ContentId>()
        var removeObserverCalls = 0
        var failPause = false

        fun answerInitial(vararg items: OfflineItem) {
            initial?.onResult(arrayListOf(*items))
        }

        fun add(item: OfflineItem) {
            observer?.onItemsAdded(listOf(item))
        }

        override fun getAllItems(callback: Callback<ArrayList<OfflineItem>>) {
            initial = callback
        }

        override fun addObserver(observer: OfflineContentProvider.Observer) {
            this.observer = observer
        }

        override fun removeObserver(observer: OfflineContentProvider.Observer) {
            if (this.observer === observer) this.observer = null
            removeObserverCalls++
        }

        override fun pauseDownload(id: ContentId) {
            if (failPause) throw IllegalStateException("provider withdrawn")
            paused += ContentId(id.namespace, id.id)
        }

        override fun resumeDownload(id: ContentId) {
            resumed += ContentId(id.namespace, id.id)
        }

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

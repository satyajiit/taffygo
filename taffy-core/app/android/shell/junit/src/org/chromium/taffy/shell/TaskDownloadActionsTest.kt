// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.shell

import android.net.Uri
import com.taffygo.browser.ui.core.model.DownloadId
import com.taffygo.browser.ui.core.model.DownloadRecord
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.async
import kotlinx.coroutines.cancelAndJoin
import kotlinx.coroutines.runBlocking
import org.chromium.base.Callback
import org.chromium.base.test.BaseRobolectricTestRunner
import org.chromium.components.offline_items_collection.ContentId
import org.chromium.components.offline_items_collection.LegacyHelpers
import org.chromium.components.offline_items_collection.OfflineContentProvider
import org.chromium.components.offline_items_collection.OfflineItem
import org.chromium.components.offline_items_collection.OfflineItemState
import org.chromium.components.offline_items_collection.OfflineItemShareInfo
import org.chromium.components.offline_items_collection.OpenParams
import org.chromium.components.offline_items_collection.ShareCallback
import org.chromium.components.offline_items_collection.VisualsCallback
import org.chromium.url.GURL
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(BaseRobolectricTestRunner::class)
class TaskDownloadActionsTest {
    @Test
    fun `completed provider PDF waits for its URI and uses the Android handoff`() = runBlocking {
        val fixture = Fixture()
        fixture.nativeOwned += "task-1" to "owned"
        fixture.provider.answer(
            item("owned"),
            item("manual"),
            item("running").apply { state = OfflineItemState.IN_PROGRESS },
            item("private").apply { isOffTheRecord = true },
            item("dangerous").apply { isDangerous = true },
            item("system").apply {
                id = ContentId(LegacyHelpers.LEGACY_ANDROID_DOWNLOAD_NAMESPACE, "owned")
            },
        )

        val offered = fixture.actions.completedForTask("task-1")
        assertEquals(listOf("report.pdf"), offered.map(DownloadRecord::fileName))
        assertEquals("application/pdf", offered.single().mimeType)
        assertTrue(fixture.provider.opened.isEmpty())
        assertTrue(fixture.actions.completedForTask("task-2").isEmpty())
        assertFalse(fixture.actions.openForTask("task-2", offered.single().id))
        assertFalse(fixture.actions.openForTask("task-1", token("manual")))

        val opening = async(Dispatchers.Unconfined) {
            fixture.actions.openForTask("task-1", offered.single().id)
        }
        assertFalse(opening.isCompleted)
        assertNull(fixture.platform.takeChooser())
        fixture.provider.reply()
        assertTrue(opening.await())
        assertNotNull(fixture.platform.takeChooser())
        // The old provider path would explicitly launch Chrome's activity.
        assertTrue(fixture.provider.opened.isEmpty())
    }

    @Test
    fun `stale offer is refused after native ownership or provider state changes`() = runBlocking {
        val fixture = Fixture()
        fixture.nativeOwned += "task-1" to "owned"
        fixture.provider.answer(item("owned"))
        val offered = fixture.actions.completedForTask("task-1").single()

        fixture.nativeOwned.clear()
        assertFalse(fixture.actions.openForTask("task-1", offered.id))
        fixture.nativeOwned += "task-1" to "owned"
        fixture.provider.update(item("owned").apply { externallyRemoved = true })
        assertFalse(fixture.actions.openForTask("task-1", offered.id))
        fixture.provider.update(item("owned").apply { isDangerous = true })
        assertFalse(fixture.actions.openForTask("task-1", offered.id))
        fixture.provider.update(item("owned").apply { isOpenable = false })
        assertFalse(fixture.actions.openForTask("task-1", offered.id))
        fixture.provider.remove(contentId("owned"))
        assertFalse(fixture.actions.openForTask("task-1", offered.id))
        assertTrue(fixture.provider.opened.isEmpty())
    }

    @Test
    fun `non PDF files keep their provider route and destroyed provider refuses Open`() = runBlocking {
        val fixture = Fixture()
        fixture.nativeOwned += "task-1" to "owned"
        fixture.provider.answer(item("owned").apply { mimeType = "text/plain" })
        assertEquals(
            "text/plain",
            fixture.actions.completedForTask("task-1").single().mimeType,
        )
        // The same filename initially said nothing about whether it was a PDF.
        assertTrue(fixture.actions.openForTask("task-1", token("owned")))
        assertEquals(listOf(contentId("owned")), fixture.provider.opened)
        fixture.provider.opened.clear()
        fixture.provider.update(item("owned"))
        val offered = fixture.actions.completedForTask("task-1").single()
        assertEquals("application/pdf", offered.mimeType)
        fixture.observer.destroy()
        assertFalse(fixture.actions.openForTask("task-1", offered.id))
        assertTrue(fixture.provider.opened.isEmpty())
    }

    @Test
    fun `authority and exact provider facts are rechecked after the URI reply`() = runBlocking {
        for (mutation in 0..7) {
            val fixture = Fixture()
            fixture.ownedPdf()
            val opening = async(Dispatchers.Unconfined) {
                fixture.actions.openForTask("task-1", token("owned"))
            }
            when (mutation) {
                0 -> fixture.nativeOwned.clear()
                1 -> fixture.provider.remove(contentId("owned"))
                2 -> fixture.provider.update(item("owned").apply { isDangerous = true })
                3 -> fixture.provider.update(item("owned").apply { isOffTheRecord = true })
                4 -> fixture.provider.update(item("owned").apply { externallyRemoved = true })
                5 -> fixture.provider.update(item("owned").apply { mimeType = "text/plain" })
                6 -> fixture.provider.update(item("owned").apply { isOpenable = false })
                7 -> fixture.actions.destroy()
            }
            fixture.provider.reply()
            assertFalse("mutation $mutation", opening.await())
            assertNull(fixture.platform.takeChooser())
            assertTrue(fixture.provider.opened.isEmpty())
        }
    }

    @Test
    fun `wrong returned identity and non content URIs never launch`() = runBlocking {
        for (mutation in 0..6) {
            val fixture = Fixture()
            fixture.ownedPdf()
            val opening = async(Dispatchers.Unconfined) {
                fixture.actions.openForTask("task-1", token("owned"))
            }
            val request = fixture.provider.requests.single()
            when (mutation) {
                0 -> fixture.provider.reply(id = contentId("other"))
                1 -> fixture.provider.reply(id = ContentId("OTHER", "owned"))
                2 -> fixture.provider.reply(uri = Uri.parse("file:///private/report.pdf"))
                3 -> fixture.provider.reply(uri = null)
                4 -> fixture.provider.reply(uri = Uri.parse("content:///report.pdf"))
                5 -> fixture.provider.reply(uri = Uri.parse("content://downloads"))
                6 -> {
                    request.first.id = "other"
                    fixture.provider.reply(id = request.first)
                }
            }
            assertFalse("mutation $mutation", opening.await())
            assertNull(fixture.platform.takeChooser())
        }
    }

    @Test
    fun `only the latest explicit request can launch and duplicate replies are inert`() = runBlocking {
        val fixture = Fixture()
        fixture.ownedPdf()
        val first = async(Dispatchers.Unconfined) {
            fixture.actions.openForTask("task-1", token("owned"))
        }
        val latest = async(Dispatchers.Unconfined) {
            fixture.actions.openForTask("task-1", token("owned"))
        }
        assertFalse(first.await())
        fixture.provider.reply(index = 0)
        assertFalse(latest.isCompleted)
        assertNull(fixture.platform.takeChooser())
        fixture.provider.reply(index = 1)
        assertTrue(latest.await())
        assertNotNull(fixture.platform.takeChooser())
        fixture.provider.reply(index = 1)
        assertNull(fixture.platform.takeChooser())
    }

    @Test
    fun `cancellation timeout and no viewer produce no successful open`() = runBlocking {
        val fixture = Fixture()
        fixture.ownedPdf()
        val opening = async(Dispatchers.Unconfined) {
            fixture.actions.openForTask("task-1", token("owned"))
        }
        opening.cancelAndJoin()
        fixture.provider.reply()
        assertTrue(opening.isCancelled)
        assertNull(fixture.platform.takeChooser())

        val timed = Fixture(timeoutMillis = 1L)
        timed.ownedPdf()
        assertFalse(timed.actions.openForTask("task-1", token("owned")))
        timed.provider.reply()
        assertNull(timed.platform.takeChooser())

        fixture.platform.removeViewer()
        val unavailable = async(Dispatchers.Unconfined) {
            fixture.actions.openForTask("task-1", token("owned"))
        }
        fixture.provider.reply()
        assertFalse(unavailable.await())
        assertNull(fixture.platform.takeChooser())
    }

    private class Fixture(timeoutMillis: Long = 10_000L) {
        val platform = TaskPdfTestPlatform()
        val provider = Provider()
        val nativeOwned = mutableSetOf<Pair<String, String>>()
        private var current = emptyList<DownloadRecord>()
        val observer = TaffyDownloadObserver(provider, publish = { current = it })
        val actions = TaskDownloadActions(
            current = { current },
            canOpen = { taskId, guid -> taskId to guid in nativeOwned },
            perform = observer::perform,
            requestFile = provider::getShareInfoForItem,
            openPdf = platform.handoff::open,
            timeoutMillis = timeoutMillis,
        )

        init {
            observer.start()
        }

        fun ownedPdf() {
            nativeOwned += "task-1" to "owned"
            provider.answer(item("owned"))
        }
    }

    private class Provider : OfflineContentProvider {
        private var observer: OfflineContentProvider.Observer? = null
        private var initial: Callback<ArrayList<OfflineItem>>? = null
        val opened = mutableListOf<ContentId>()
        val requests = mutableListOf<Pair<ContentId, ShareCallback>>()

        fun reply(
            index: Int = requests.lastIndex,
            id: ContentId = contentId("owned"),
            uri: Uri? = TaskPdfTestPlatform.URI,
        ) {
            requests[index].second.onShareInfoAvailable(id, OfflineItemShareInfo().apply { this.uri = uri })
        }

        fun answer(vararg items: OfflineItem) {
            initial?.onResult(arrayListOf(*items))
        }

        fun update(item: OfflineItem) {
            observer?.onItemsAdded(listOf(item))
        }

        fun remove(id: ContentId) {
            observer?.onItemRemoved(id)
        }

        override fun addObserver(observer: OfflineContentProvider.Observer) {
            this.observer = observer
        }

        override fun removeObserver(observer: OfflineContentProvider.Observer) {
            if (this.observer === observer) this.observer = null
        }

        override fun getAllItems(callback: Callback<ArrayList<OfflineItem>>) {
            initial = callback
        }

        override fun openItem(params: OpenParams, id: ContentId) {
            opened += ContentId(id.namespace, id.id)
        }

        override fun pauseDownload(id: ContentId) = Unit
        override fun resumeDownload(id: ContentId) = Unit
        override fun cancelDownload(id: ContentId) = Unit
        override fun removeItem(id: ContentId) = Unit
        override fun validateDangerousDownload(id: ContentId) = Unit
        override fun getItemById(id: ContentId, callback: Callback<OfflineItem>) = Unit
        override fun getVisualsForItem(id: ContentId, callback: VisualsCallback) = Unit
        override fun getShareInfoForItem(id: ContentId, callback: ShareCallback) {
            requests += id to callback
        }
        override fun renameItem(id: ContentId, name: String, callback: Callback<Int>) = Unit
    }

    private companion object {
        fun item(guid: String) = OfflineItem().apply {
            id = contentId(guid)
            title = "report.pdf"
            mimeType = "application/pdf"
            state = OfflineItemState.COMPLETE
            isOpenable = true
            totalSizeBytes = 10
            receivedBytes = 10
            url = GURL.emptyGURL()
        }

        fun contentId(guid: String) = ContentId(LegacyHelpers.LEGACY_DOWNLOAD_NAMESPACE, guid)

        fun token(guid: String): DownloadId {
            val namespace = LegacyHelpers.LEGACY_DOWNLOAD_NAMESPACE
            return DownloadId("${namespace.length}:$namespace$guid")
        }
    }
}

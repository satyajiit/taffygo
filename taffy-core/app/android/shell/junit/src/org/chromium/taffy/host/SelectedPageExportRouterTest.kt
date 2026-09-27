// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.host

import com.taffygo.browser.ui.core.page.PageIntelligenceClient
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.CoroutineStart
import kotlinx.coroutines.async
import kotlinx.coroutines.runBlocking
import org.chromium.base.test.BaseRobolectricTestRunner
import org.junit.Assert.assertEquals
import org.junit.Test
import org.junit.runner.RunWith
import taffy.core_api.CoreApiSubmissionStatus
import taffy.core_api.PageInspectorAvailability
import taffy.core_api.PageInspectorDocumentsView
import taffy.core_api.PageInspectorSnapshotResult
import taffy.core_api.PageSnapshotExportAvailability
import taffy.core_api.PageSnapshotExportFormat
import taffy.core_api.PageSnapshotExportResult

@RunWith(BaseRobolectricTestRunner::class)
class SelectedPageExportRouterTest {
    @Test
    fun `selection change refuses a late origin-tab result`() = runBlocking {
        val first = RecordingPageClient()
        var selected: PageIntelligenceClient? = first
        val router = SelectedPageExportRouter { selected }
        val exported = async(start = CoroutineStart.UNDISPATCHED) {
            router.exportSnapshot(REQUEST_ID, DOCUMENT_ID, FORMAT)
        }

        selected = RecordingPageClient()
        router.selectionChanged()
        first.complete()

        assertEquals(PageSnapshotExportAvailability.STALE_DOCUMENT, exported.await().availability)
        assertEquals(listOf(REQUEST_ID), first.cancelled)
    }

    @Test
    fun `cancel after tab switch reaches only the originating client`() = runBlocking {
        val first = RecordingPageClient()
        val second = RecordingPageClient()
        var selected: PageIntelligenceClient? = first
        val router = SelectedPageExportRouter { selected }
        val exported = async(start = CoroutineStart.UNDISPATCHED) {
            router.exportSnapshot(REQUEST_ID, DOCUMENT_ID, FORMAT)
        }

        selected = second
        router.selectionChanged()
        assertEquals(CoreApiSubmissionStatus.ACCEPTED, router.cancelExport(REQUEST_ID))
        first.complete()

        assertEquals(listOf(REQUEST_ID), first.cancelled)
        assertEquals(emptyList<String>(), second.cancelled)
        assertEquals(PageSnapshotExportAvailability.STALE_DOCUMENT, exported.await().availability)
    }

    @Test
    fun `one request identity cannot be delegated twice`() = runBlocking {
        val client = RecordingPageClient()
        val router = SelectedPageExportRouter { client }
        val first = async(start = CoroutineStart.UNDISPATCHED) {
            router.exportSnapshot(REQUEST_ID, DOCUMENT_ID, FORMAT)
        }

        val duplicate = router.exportSnapshot(REQUEST_ID, DOCUMENT_ID, FORMAT)

        assertEquals(PageSnapshotExportAvailability.REPLAY_CONFLICT, duplicate.availability)
        assertEquals(listOf(REQUEST_ID), client.exports)
        client.complete()
        first.await()
        Unit
    }

    @Test
    fun `close refuses a callback from the former window lifetime`() = runBlocking {
        val client = RecordingPageClient()
        val router = SelectedPageExportRouter { client }
        val exported = async(start = CoroutineStart.UNDISPATCHED) {
            router.exportSnapshot(REQUEST_ID, DOCUMENT_ID, FORMAT)
        }

        router.close()
        client.complete()

        assertEquals(PageSnapshotExportAvailability.STALE_DOCUMENT, exported.await().availability)
        assertEquals(
            PageSnapshotExportAvailability.NO_SELECTED_PAGE,
            router.exportSnapshot("new-request", DOCUMENT_ID, FORMAT).availability,
        )
    }

    private class RecordingPageClient : PageIntelligenceClient {
        private val completion = CompletableDeferred<PageSnapshotExportResult>()
        val exports = mutableListOf<String>()
        val cancelled = mutableListOf<String>()

        override val isAvailable = true

        override suspend fun documents() = PageInspectorDocumentsView(
            PageInspectorAvailability.NO_SELECTED_PAGE,
            emptyList(),
        )

        override suspend fun snapshot(documentId: String) = PageInspectorSnapshotResult(
            PageInspectorAvailability.NO_SELECTED_PAGE,
            null,
        )

        override suspend fun exportSnapshot(
            requestId: String,
            documentId: String,
            format: PageSnapshotExportFormat,
        ): PageSnapshotExportResult {
            exports += requestId
            return completion.await()
        }

        override suspend fun cancelExport(requestId: String): CoreApiSubmissionStatus {
            cancelled += requestId
            return CoreApiSubmissionStatus.ACCEPTED
        }

        fun complete() {
            completion.complete(
                PageSnapshotExportResult(PageSnapshotExportAvailability.INVALID_RESPONSE, null),
            )
        }
    }

    private companion object {
        const val REQUEST_ID = "page-export-1"
        const val DOCUMENT_ID = "selected-page"
        val FORMAT = PageSnapshotExportFormat.MARKDOWN
    }
}

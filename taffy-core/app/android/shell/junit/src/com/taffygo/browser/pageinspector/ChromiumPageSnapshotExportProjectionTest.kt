// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.pageinspector

import org.chromium.taffy.core_api.mojom.PageSnapshotExportAvailability as MojoAvailability
import org.chromium.taffy.core_api.mojom.PageSnapshotExportFormat as MojoFormat
import org.chromium.taffy.core_api.mojom.PageSnapshotExportResult as MojoResult
import org.chromium.taffy.core_api.mojom.PageSnapshotExportView as MojoView
import org.chromium.base.test.BaseRobolectricTestRunner
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test
import org.junit.runner.RunWith
import taffy.core_api.MAX_PAGE_SNAPSHOT_EXPORT_BYTES
import taffy.core_api.PageSnapshotExportAvailability
import taffy.core_api.PageSnapshotExportFormat

@RunWith(BaseRobolectricTestRunner::class)
class ChromiumPageSnapshotExportProjectionTest {
    @Test
    fun `exact immutable bytes and provenance cross the generated seam`() {
        val content = "# Page\n\n[withheld]\n".toByteArray()
        val projected = validResult(content).toCoreExportResult(
            REQUEST_ID,
            DOCUMENT_ID,
            PageSnapshotExportFormat.MARKDOWN,
        )

        assertEquals(PageSnapshotExportAvailability.AVAILABLE, projected.availability)
        val export = requireNotNull(projected.snapshot_export)
        assertEquals(7uL, export.document_revision)
        assertEquals("https://example.test", export.origin)
        assertArrayEquals(content, export.content)
        content[0] = '!'.code.toByte()
        assertEquals('#'.code.toByte(), export.content[0])
        assertEquals(3u, export.redacted_field_count)
        assertEquals(2u, export.withheld_field_count)
        assertEquals(1_725_000_000_123uL, export.captured_at_epoch_ms)
        assertEquals(true, export.source_query_withheld)
        assertEquals(true, export.source_fragment_withheld)
        assertEquals(true, export.secure_context)
    }

    @Test
    fun `oversize and mismatched correlation fail closed without bytes`() {
        val oversize = validResult(ByteArray(MAX_PAGE_SNAPSHOT_EXPORT_BYTES + 1))
            .toCoreExportResult(REQUEST_ID, DOCUMENT_ID, PageSnapshotExportFormat.MARKDOWN)
        assertEquals(PageSnapshotExportAvailability.INVALID_RESPONSE, oversize.availability)
        assertNull(oversize.snapshot_export)

        val mismatch = validResult("safe".toByteArray())
            .toCoreExportResult("another-request", DOCUMENT_ID, PageSnapshotExportFormat.MARKDOWN)
        assertEquals(PageSnapshotExportAvailability.INVALID_RESPONSE, mismatch.availability)
        assertNull(mismatch.snapshot_export)

        val spoofedType = validResult("safe".toByteArray()).apply {
            checkNotNull(snapshotExport).mimeType = "text/html"
        }.toCoreExportResult(REQUEST_ID, DOCUMENT_ID, PageSnapshotExportFormat.MARKDOWN)
        assertEquals(PageSnapshotExportAvailability.INVALID_RESPONSE, spoofedType.availability)
        assertNull(spoofedType.snapshot_export)

        val invalidCapture = validResult("safe".toByteArray()).apply {
            checkNotNull(snapshotExport).capturedAtEpochMs = 0L
        }.toCoreExportResult(REQUEST_ID, DOCUMENT_ID, PageSnapshotExportFormat.MARKDOWN)
        assertEquals(PageSnapshotExportAvailability.INVALID_RESPONSE, invalidCapture.availability)
        assertNull(invalidCapture.snapshot_export)
    }

    @Test
    fun `typed refusal remains content free`() {
        val result = MojoResult().apply {
            availability = MojoAvailability.STALE_DOCUMENT
            snapshotExport = null
        }.toCoreExportResult(REQUEST_ID, DOCUMENT_ID, PageSnapshotExportFormat.MARKDOWN)

        assertEquals(PageSnapshotExportAvailability.STALE_DOCUMENT, result.availability)
        assertNull(result.snapshot_export)
    }

    private fun validResult(content: ByteArray) = MojoResult().apply {
        availability = MojoAvailability.AVAILABLE
        snapshotExport = MojoView().apply {
            requestId = REQUEST_ID
            documentId = DOCUMENT_ID
            documentRevision = 7L
            origin = "https://example.test"
            format = MojoFormat.MARKDOWN
            mimeType = "text/markdown"
            suggestedFileName = "taffy-page-snapshot.md"
            this.content = content
            nodeCount = 4
            redactedFieldCount = 3
            suppressedSecretValueCount = 1
            withheldFieldCount = 2
            capturedAtEpochMs = 1_725_000_000_123L
            sourceQueryWithheld = true
            sourceFragmentWithheld = true
            secureContext = true
        }
    }

    private companion object {
        const val REQUEST_ID = "page-export-1"
        const val DOCUMENT_ID = "selected-page"
    }
}

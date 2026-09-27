// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.workspaces

import com.taffygo.browser.ui.core.model.ExportFormat
import java.io.ByteArrayOutputStream
import java.io.IOException
import java.io.OutputStream
import kotlinx.coroutines.CancellationException
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class WorkspaceExportDocumentWriterTest {
    @Test
    fun `formats use fixed safe names and exact MIME types`() {
        assertEquals(
            ExportDocumentSpec("text/markdown", "taffy-workspace.md"),
            ExportFormat.MARKDOWN.workspaceDocumentSpec(),
        )
        assertEquals(
            ExportDocumentSpec("text/csv", "taffy-workspace.csv"),
            ExportFormat.COMMA_SEPARATED.workspaceDocumentSpec(),
        )
        assertEquals(
            ExportDocumentSpec("text/markdown", "taffy-library.md"),
            ExportFormat.MARKDOWN.libraryDocumentSpec(),
        )
        assertEquals(
            ExportDocumentSpec("text/csv", "taffy-library.csv"),
            ExportFormat.COMMA_SEPARATED.libraryDocumentSpec(),
        )
    }

    @Test
    fun `writer emits the Rust rendered UTF-8 bytes and closes the document`() {
        val output = RecordingOutputStream()

        val written = writeWorkspaceExport("# Café\n") { output }

        assertTrue(written)
        assertEquals("# Café\n", output.toByteArray().decodeToString())
        assertTrue(output.closed)
    }

    @Test
    fun `missing or failing document providers are a failed write`() {
        assertFalse(writeWorkspaceExport("content") { null })
        assertFalse(
            writeWorkspaceExport("content") {
                object : OutputStream() {
                    override fun write(value: Int) = throw IOException("provider stopped")
                }
            },
        )
        assertFalse(writeWorkspaceExport("content") { throw SecurityException("grant revoked") })
    }

    @Test(expected = CancellationException::class)
    fun `writer never turns coroutine cancellation into a provider failure`() {
        writeWorkspaceExport("content") { throw CancellationException("screen closed") }
    }

    private class RecordingOutputStream : ByteArrayOutputStream() {
        var closed = false

        override fun close() {
            closed = true
            super.close()
        }
    }
}

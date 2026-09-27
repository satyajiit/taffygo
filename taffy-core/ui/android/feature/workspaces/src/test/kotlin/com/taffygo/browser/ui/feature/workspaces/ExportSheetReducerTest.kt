// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.workspaces

import com.taffygo.browser.ui.core.model.ExportFormat
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * Screen SCR-309's reducer. The preview is produced by the same renderer that
 * would write the file, so choosing a format is a re-render and never a
 * description of one.
 */
class ExportSheetReducerTest {

    @Test
    fun `choosing a format re-renders the preview through the renderer`() {
        val state = reduceExportSheet(
            state = ExportSheetUiState(),
            intent = ExportSheetIntent.Select(ExportFormat.COMMA_SEPARATED),
            render = { format -> "rendered as ${format.label}" },
            workspaceExists = { true },
        )

        assertEquals(ExportFormat.COMMA_SEPARATED, state.selected)
        assertEquals("rendered as comma_separated", state.preview)
        assertFalse(state.missing)
        assertTrue(state.canExport)
    }

    @Test
    fun `a workspace the renderer cannot find leaves nothing to export`() {
        val state = reduceExportSheet(
            state = ExportSheetUiState(preview = "an older preview"),
            intent = ExportSheetIntent.Select(ExportFormat.MARKDOWN),
            render = { null },
            workspaceExists = { false },
        )

        assertTrue(state.missing)
        assertEquals("", state.preview)
        assertFalse(state.canExport)
    }

    @Test
    fun `an empty render is not something to export either`() {
        val state = reduceExportSheet(
            state = ExportSheetUiState(),
            intent = ExportSheetIntent.Select(ExportFormat.MARKDOWN),
            render = { "" },
            workspaceExists = { true },
        )

        assertFalse(state.canExport)
        assertFalse(state.missing)
    }

    @Test
    fun `a loading sheet is not something to export`() {
        val state = ExportSheetUiState(preview = "not yet", loading = true)

        assertTrue(state.loading)
        assertFalse(state.canExport)
    }

    @Test
    fun `both formats the milestone offers are on the sheet`() {
        assertEquals(
            listOf(ExportFormat.MARKDOWN, ExportFormat.COMMA_SEPARATED),
            ExportSheetUiState().formats,
        )
    }

    @Test
    fun `export lifecycle is explicit and only active work disables another write`() {
        val state = ExportSheetUiState(preview = "# Retention policies")
        val choosing = reduceExportSheet(
            state,
            ExportSheetIntent.Export,
            { "ignored" },
            { true },
        )
        val writing = reduceExportSheet(
            choosing,
            ExportSheetIntent.DestinationSelected,
            { "ignored" },
            { true },
        )

        assertEquals(ExportStatus.CHOOSING_DESTINATION, choosing.exportStatus)
        assertFalse(choosing.canExport)
        assertEquals(ExportStatus.WRITING, writing.exportStatus)
        assertFalse(writing.canExport)
        assertEquals(
            ExportStatus.SUCCEEDED,
            reduceExportSheet(
                writing,
                ExportSheetIntent.WriteSucceeded,
                { "ignored" },
                { true },
            ).exportStatus,
        )
        assertEquals(
            ExportStatus.FAILED,
            reduceExportSheet(
                writing,
                ExportSheetIntent.WriteFailed,
                { "ignored" },
                { true },
            ).exportStatus,
        )
        assertEquals(
            ExportStatus.CANCELLED,
            reduceExportSheet(
                choosing,
                ExportSheetIntent.DestinationCancelled,
                { "ignored" },
                { true },
            ).exportStatus,
        )
        assertEquals(
            state,
            reduceExportSheet(state, ExportSheetIntent.Close, { "ignored" }, { true }),
        )
    }

    @Test
    fun `choosing another format clears the prior write result`() {
        val state = ExportSheetUiState(
            preview = "old",
            exportStatus = ExportStatus.SUCCEEDED,
        )

        val selected = reduceExportSheet(
            state,
            ExportSheetIntent.Select(ExportFormat.COMMA_SEPARATED),
            { "new" },
            { true },
        )

        assertEquals(ExportStatus.IDLE, selected.exportStatus)
    }

    @Test
    fun `preview retains only a bounded first-lines projection`() {
        val longLine = "x".repeat(10_000)
        val manyLines = (1..30).joinToString("\n") { "line $it" }

        assertEquals(4_096, exportPreview(longLine).length)
        assertEquals(12, exportPreview(manyLines).lineSequence().count())
    }
}

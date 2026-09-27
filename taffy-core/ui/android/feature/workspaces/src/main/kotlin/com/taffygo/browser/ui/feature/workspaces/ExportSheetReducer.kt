// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.workspaces

import com.taffygo.browser.ui.core.model.ExportFormat

/** Choosing a format re-renders the preview from the same renderer. */
internal fun reduceExportSheet(
    state: ExportSheetUiState,
    intent: ExportSheetIntent,
    render: (ExportFormat) -> String?,
    workspaceExists: () -> Boolean,
): ExportSheetUiState = when (intent) {
    is ExportSheetIntent.Select -> {
        val rendered = render(intent.format)?.let(::exportPreview)
        state.copy(
            selected = intent.format,
            preview = rendered.orEmpty(),
            missing = !workspaceExists(),
            exportStatus = ExportStatus.IDLE,
        )
    }
    ExportSheetIntent.Export -> if (state.canExport) {
        state.copy(exportStatus = ExportStatus.CHOOSING_DESTINATION)
    } else {
        state
    }
    ExportSheetIntent.DestinationSelected -> if (
        state.exportStatus == ExportStatus.CHOOSING_DESTINATION
    ) {
        state.copy(exportStatus = ExportStatus.WRITING)
    } else {
        state
    }
    ExportSheetIntent.DestinationCancelled -> if (
        state.exportStatus == ExportStatus.CHOOSING_DESTINATION
    ) {
        state.copy(exportStatus = ExportStatus.CANCELLED)
    } else {
        state
    }
    ExportSheetIntent.WriteSucceeded -> if (state.exportStatus == ExportStatus.WRITING) {
        state.copy(exportStatus = ExportStatus.SUCCEEDED)
    } else {
        state
    }
    ExportSheetIntent.WriteFailed -> if (
        state.exportStatus == ExportStatus.CHOOSING_DESTINATION ||
        state.exportStatus == ExportStatus.WRITING
    ) {
        state.copy(exportStatus = ExportStatus.FAILED)
    } else {
        state
    }
    ExportSheetIntent.Close -> state
}

/** The UI retains only a bounded first-lines projection, never the whole file. */
internal fun exportPreview(content: String): String = content
    .take(MAX_EXPORT_PREVIEW_CHARACTERS)
    .lineSequence()
    .take(MAX_EXPORT_PREVIEW_LINES)
    .joinToString("\n")

private const val MAX_EXPORT_PREVIEW_CHARACTERS = 4_096
private const val MAX_EXPORT_PREVIEW_LINES = 12

// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.workspaces

import com.taffygo.browser.ui.core.model.ExportFormat

/**
 * Screen SCR-309 — the export sheet.
 *
 * [preview] is the file's own first lines, rendered by the same code that would
 * write it. The sheet shows what will be written rather than describing it.
 */
data class ExportSheetUiState(
    /** The formats offered at this milestone. */
    val formats: List<ExportFormat> = ExportFormat.entries,
    /** The one chosen. */
    val selected: ExportFormat = ExportFormat.MARKDOWN,
    /** The first lines of the file that would be written. */
    val preview: String = "",
    /**
     * True until the repository has published the requested source. A first
     * frame with no workspace or Library collection is this, not [missing].
     */
    val loading: Boolean = false,
    /** Whether the workspace or Library collection resolved at all. */
    val missing: Boolean = false,
    /** The trusted create-document flow's current terminal or active state. */
    val exportStatus: ExportStatus = ExportStatus.IDLE,
) {
    /** Whether there is a file to write. */
    val canExport: Boolean
        get() = !loading &&
            !missing &&
            preview.isNotBlank() &&
            exportStatus != ExportStatus.CHOOSING_DESTINATION &&
            exportStatus != ExportStatus.WRITING
}

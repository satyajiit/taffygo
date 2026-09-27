// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.page

import taffy.core_api.CoreApiSubmissionStatus
import taffy.core_api.PageInspectorDocumentsView
import taffy.core_api.PageInspectorSnapshotResult
import taffy.core_api.PageSnapshotExportFormat
import taffy.core_api.PageSnapshotExportResult

/** Window projection over the browser-selected tab's UI-safe page intelligence. */
interface PageIntelligenceRepository {
    val isAvailable: Boolean

    /** Every document available to inspect in the selected WebContents. */
    suspend fun documents(): PageInspectorDocumentsView

    /** One generated, browser-redacted projection, or a closed typed refusal. */
    suspend fun snapshot(documentId: String): PageInspectorSnapshotResult

    /** Render a fresh exact page observation inside the sandboxed core. */
    suspend fun exportSnapshot(
        requestId: String,
        documentId: String,
        format: PageSnapshotExportFormat,
    ): PageSnapshotExportResult

    suspend fun cancelExport(requestId: String): CoreApiSubmissionStatus
}

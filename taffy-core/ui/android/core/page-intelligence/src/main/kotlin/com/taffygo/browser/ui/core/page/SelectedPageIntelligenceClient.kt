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

/**
 * Window-owned multiplexer over tab-scoped [PageIntelligenceClient] objects.
 *
 * Chromium changes the selected delegate when a tab moves or selection
 * changes; it never extends a tab client's WebContents lifetime.
 */
interface SelectedPageIntelligenceClient {
    val isAvailable: Boolean
    suspend fun documents(): PageInspectorDocumentsView
    suspend fun snapshot(documentId: String): PageInspectorSnapshotResult
    suspend fun exportSnapshot(
        requestId: String,
        documentId: String,
        format: PageSnapshotExportFormat,
    ): PageSnapshotExportResult
    suspend fun cancelExport(requestId: String): CoreApiSubmissionStatus
}

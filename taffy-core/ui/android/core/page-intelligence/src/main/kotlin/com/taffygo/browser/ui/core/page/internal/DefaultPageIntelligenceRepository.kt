// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.page.internal

import com.taffygo.browser.ui.core.page.PageIntelligenceRepository
import com.taffygo.browser.ui.core.page.SelectedPageIntelligenceClient
import taffy.core_api.CoreApiSubmissionStatus
import taffy.core_api.PageInspectorDocumentsView
import taffy.core_api.PageInspectorSnapshotResult
import taffy.core_api.PageSnapshotExportFormat
import taffy.core_api.PageSnapshotExportResult

/** Page intelligence from the tab currently selected by the owning window. */
internal class DefaultPageIntelligenceRepository(
    private val client: SelectedPageIntelligenceClient,
) : PageIntelligenceRepository {
    override val isAvailable: Boolean
        get() = client.isAvailable

    override suspend fun documents(): PageInspectorDocumentsView = client.documents()

    override suspend fun snapshot(documentId: String): PageInspectorSnapshotResult =
        client.snapshot(documentId)

    override suspend fun exportSnapshot(
        requestId: String,
        documentId: String,
        format: PageSnapshotExportFormat,
    ): PageSnapshotExportResult = client.exportSnapshot(requestId, documentId, format)

    override suspend fun cancelExport(requestId: String): CoreApiSubmissionStatus =
        client.cancelExport(requestId)
}

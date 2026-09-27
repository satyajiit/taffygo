// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.host

import com.taffygo.browser.ui.core.page.PageIntelligenceClient
import taffy.core_api.CoreApiSubmissionStatus
import taffy.core_api.PageInspectorAvailability
import taffy.core_api.PageInspectorDocumentsView
import taffy.core_api.PageInspectorSnapshotResult
import taffy.core_api.PageSnapshotExportAvailability
import taffy.core_api.PageSnapshotExportFormat
import taffy.core_api.PageSnapshotExportResult

/** Close-aware Dagger bind slot populated immediately after the Tab component is built. */
internal class TaffyTabPageClientSlot : PageIntelligenceClient {
    private var delegate: PageIntelligenceClient? = null
    private var closed = false

    override val isAvailable: Boolean
        get() = delegate?.isAvailable == true && !closed

    fun bind(client: PageIntelligenceClient) {
        check(!closed && delegate == null) { "A Tab page client may be bound exactly once" }
        delegate = client
    }

    fun close() {
        if (closed) return
        closed = true
        delegate = null
    }

    override suspend fun documents(): PageInspectorDocumentsView =
        delegate?.takeUnless { closed }?.documents()
            ?: PageInspectorDocumentsView(PageInspectorAvailability.NO_SELECTED_PAGE, emptyList())

    override suspend fun snapshot(documentId: String): PageInspectorSnapshotResult =
        delegate?.takeUnless { closed }?.snapshot(documentId)
            ?: PageInspectorSnapshotResult(PageInspectorAvailability.NO_SELECTED_PAGE, null)

    override suspend fun exportSnapshot(
        requestId: String,
        documentId: String,
        format: PageSnapshotExportFormat,
    ): PageSnapshotExportResult =
        delegate?.takeUnless { closed }?.exportSnapshot(requestId, documentId, format)
            ?: PageSnapshotExportResult(PageSnapshotExportAvailability.NO_SELECTED_PAGE, null)

    override suspend fun cancelExport(requestId: String): CoreApiSubmissionStatus =
        delegate?.takeUnless { closed }?.cancelExport(requestId)
            ?: CoreApiSubmissionStatus.INVALID_REQUEST
}

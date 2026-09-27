// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.page

import taffy.core_api.PageInspectorDocumentsView
import taffy.core_api.PageInspectorSnapshotResult
import taffy.core_api.CoreApiSubmissionStatus
import taffy.core_api.PageSnapshotExportFormat
import taffy.core_api.PageSnapshotExportResult

/**
 * Where a page snapshot comes from.
 *
 * The production implementation is created by the WebContents owner and
 * exposes only the generated, UI-safe Core API projection under the current
 * page epoch. Renderer records and raw graph bytes are not representable here.
 * Golden documents implement this interface only from test source sets; they
 * are not in the product graph.
 *
 * ## Honesty
 *
 * [isAvailable] is this seam's version of what `PageSurface.isLive` is for the
 * page and what `TaskExecutor.isStandIn` is for the executor: the one question
 * a surface may ask about what is behind the seam, so that it draws what is
 * actually there rather than a way in to a refusal.
 */
interface PageIntelligenceClient {

    /** Whether this exact WebContents-scoped generated transport is live. */
    val isAvailable: Boolean

    /** Typed availability and bounded documents projected for this WebContents. */
    suspend fun documents(): PageInspectorDocumentsView

    /** Read one policy-admitted, browser-redacted Core API projection. */
    suspend fun snapshot(documentId: String): PageInspectorSnapshotResult

    /** Ask sandboxed Rust to render one exact observation into immutable bytes. */
    suspend fun exportSnapshot(
        requestId: String,
        documentId: String,
        format: PageSnapshotExportFormat,
    ): PageSnapshotExportResult

    /** Cancel only the export named by [requestId]. */
    suspend fun cancelExport(requestId: String): CoreApiSubmissionStatus
}

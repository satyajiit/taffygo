// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.api

import taffy.core_api.WorkspaceExportFormat

/**
 * The resident Library at an exact published revision: searching it,
 * refreshing it, keeping a cited fact, removing an entry, exporting it.
 */
interface LibraryCoreApiClient {
    /** Search the exact resident Library revision with a browser-stamped time. */
    suspend fun searchLibrary(requestId: String, query: String, limit: UInt) {
        throw CoreApiSubmissionException(CoreApiSubmissionException.Reason.PROTOCOL_VIOLATION)
    }

    /** Start the exact content-free Library refresh preview the person approved. */
    suspend fun startLibraryRefresh(
        previewId: String,
        collectionId: String,
        expectedLibraryRevision: ULong,
        expectedWorkspaceRevision: ULong,
        sourceCount: UInt,
    ) {
        throw CoreApiSubmissionException(CoreApiSubmissionException.Reason.PROTOCOL_VIOLATION)
    }

    /** Explicitly keep one cited fact from the exact saved workspace revision shown. */
    suspend fun saveLibraryFact(
        workspaceId: String,
        expectedWorkspaceRevision: ULong,
        factId: String,
        expectedLibraryRevision: ULong,
        expectedEntryRevision: ULong,
    ) {
        throw CoreApiSubmissionException(CoreApiSubmissionException.Reason.PROTOCOL_VIOLATION)
    }

    /** Delete one Library entry using both exact revisions published to the surface. */
    suspend fun removeLibraryEntry(
        entryId: String,
        expectedLibraryRevision: ULong,
        expectedEntryRevision: ULong,
    ) {
        throw CoreApiSubmissionException(CoreApiSubmissionException.Reason.PROTOCOL_VIOLATION)
    }

    /** Ask Rust for a bounded deterministic Library or collection export. */
    suspend fun requestLibraryExport(
        requestId: String,
        expectedLibraryRevision: ULong,
        collectionId: String?,
        format: WorkspaceExportFormat,
    ) {
        throw CoreApiSubmissionException(CoreApiSubmissionException.Reason.PROTOCOL_VIOLATION)
    }
}

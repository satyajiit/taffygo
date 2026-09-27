// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.api

import taffy.core_api.WorkspaceExportFormat

/**
 * One task workspace at an exact published revision: correcting it, keeping
 * it, renaming it, exporting it, and discarding or deleting it.
 */
interface WorkspaceCoreApiClient {
    /** Record a correction beside the source value at an exact workspace revision. */
    suspend fun correctWorkspaceFact(
        workspaceId: String,
        expectedRevision: ULong,
        factId: String,
        value: String,
    )

    /** Exclude one source at an exact workspace revision. */
    suspend fun excludeWorkspaceSource(
        workspaceId: String,
        expectedRevision: ULong,
        sourceId: String,
    )

    /** Ask Rust to produce one deterministic, bounded workspace export. */
    suspend fun requestWorkspaceExport(
        requestId: String,
        workspaceId: String,
        expectedRevision: ULong,
        format: WorkspaceExportFormat,
    )

    /** Explicitly keep one terminal task result at the revision the person reviewed. */
    suspend fun saveWorkspace(workspaceId: String, expectedRevision: ULong)

    /** Change only the display name of one saved workspace at an exact revision. */
    suspend fun renameWorkspace(
        workspaceId: String,
        expectedRevision: ULong,
        displayName: String,
    ) {
        throw CoreApiSubmissionException(CoreApiSubmissionException.Reason.PROTOCOL_VIOLATION)
    }

    /** Confirm deletion with the exact challenge published for this saved revision. */
    suspend fun deleteWorkspace(
        workspaceId: String,
        expectedRevision: ULong,
        confirmationToken: String,
    ) {
        throw CoreApiSubmissionException(CoreApiSubmissionException.Reason.PROTOCOL_VIOLATION)
    }

    /** Explicitly discard one terminal unsaved task workspace. */
    suspend fun discardWorkspace(workspaceId: String, expectedRevision: ULong) {
        throw CoreApiSubmissionException(CoreApiSubmissionException.Reason.PROTOCOL_VIOLATION)
    }
}

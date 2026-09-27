// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.api

import taffy.core_api.MemoryScopeKind
import taffy.core_api.MemorySensitivity
import taffy.core_api.MemoryWorkspaceView

/**
 * The resident Memory at an exact published revision: searching it, and
 * creating, replacing or physically deleting one visible record.
 */
interface MemoryCoreApiClient {
    /** Search the exact resident Memory revision without exposing private state. */
    suspend fun searchMemory(requestId: String, query: String, limit: UInt) {
        throw CoreApiSubmissionException(CoreApiSubmissionException.Reason.PROTOCOL_VIOLATION)
    }

    /** Explicitly create or replace one visible Memory record at exact revisions. */
    suspend fun upsertMemory(
        memoryId: String?,
        statement: String,
        scopeKind: MemoryScopeKind,
        scopeWorkspace: MemoryWorkspaceView?,
        sensitivity: MemorySensitivity,
        expectedMemoryRevision: ULong,
        expectedRecordRevision: ULong,
        expiresAtEpochMillis: ULong,
    ) {
        throw CoreApiSubmissionException(CoreApiSubmissionException.Reason.PROTOCOL_VIOLATION)
    }

    /** Physically delete one Memory record using both published revisions. */
    suspend fun deleteMemory(
        memoryId: String,
        expectedMemoryRevision: ULong,
        expectedRecordRevision: ULong,
    ) {
        throw CoreApiSubmissionException(CoreApiSubmissionException.Reason.PROTOCOL_VIOLATION)
    }
}

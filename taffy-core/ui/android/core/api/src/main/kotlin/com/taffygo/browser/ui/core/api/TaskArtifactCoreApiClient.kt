// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.api

import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.emptyFlow
import taffy.core_api.TaskArtifactKind

/** Explicit keep and transient file-delivery seam for task artifacts. */
interface TaskArtifactCoreApiClient {
    /** No replay: a file is delivered only to the request that asked for it. */
    val taskArtifactExport: Flow<TaskArtifactExportReport>
        get() = emptyFlow()

    suspend fun acceptTaskArtifact(taskId: String, artifactId: String) {
        throw CoreApiSubmissionException(CoreApiSubmissionException.Reason.PROTOCOL_VIOLATION)
    }

    suspend fun requestTaskArtifactExport(
        requestId: String,
        taskId: String,
        artifactId: String,
        kind: TaskArtifactKind,
    ) {
        throw CoreApiSubmissionException(CoreApiSubmissionException.Reason.PROTOCOL_VIOLATION)
    }
}

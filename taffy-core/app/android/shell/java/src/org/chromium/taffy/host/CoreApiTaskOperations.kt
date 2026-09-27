// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.host

import org.chromium.taffy.core_api.mojom.TaffyProfileCoreApi
import taffy.core_api.TaskConsentPreview
import taffy.core_api.TaskArtifactKind
import taffy.core_api.TaskTemplateId

/** Submits task lifecycle commands over the profile-owned Core API pipe. */
internal class CoreApiTaskOperations(
    private val proxy: TaffyProfileCoreApi,
    private val submissions: CoreApiSubmissionDispatcher,
) {
    suspend fun start(
        goal: String,
        templateId: TaskTemplateId,
        consentPreview: TaskConsentPreview,
        workspaceId: String?,
        skillOfferId: String? = null,
    ) = submissions.submit { callback ->
        proxy.startTask(
            goal,
            templateId.wire.toInt(),
            workspaceId,
            consentPreview.toMojo(),
            skillOfferId,
            callback,
        )
    }

    suspend fun cancel(taskId: String) =
        submissions.submit { callback -> proxy.cancelTask(taskId, callback) }

    suspend fun pause(taskId: String) =
        submissions.submit { callback -> proxy.pauseTask(taskId, callback) }

    suspend fun resume(taskId: String) =
        submissions.submit { callback -> proxy.resumeTask(taskId, callback) }

    suspend fun takeOver(taskId: String) =
        submissions.submit { callback -> proxy.takeOver(taskId, callback) }

    suspend fun completeHandover(taskId: String) =
        submissions.submit { callback -> proxy.completeHandover(taskId, callback) }

    suspend fun supplyUserInput(taskId: String, answer: String) =
        submissions.submit { callback -> proxy.supplyUserInput(taskId, answer, callback) }

    suspend fun followUp(taskId: String, question: String) =
        submissions.submit { callback -> proxy.followUp(taskId, question, callback) }

    suspend fun approveAction(taskId: String, actionId: String) =
        submissions.submit { callback -> proxy.approveAction(taskId, actionId, callback) }

    suspend fun acceptArtifact(taskId: String, artifactId: String) =
        submissions.submit { callback ->
            proxy.acceptTaskArtifact(taskId, artifactId, callback)
        }

    suspend fun requestArtifactExport(
        requestId: String,
        taskId: String,
        artifactId: String,
        kind: TaskArtifactKind,
    ) = submissions.submit { callback ->
        proxy.requestTaskArtifactExport(
            requestId,
            taskId,
            artifactId,
            kind.wire.toInt(),
            callback,
        )
    }
}

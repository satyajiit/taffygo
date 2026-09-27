// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.host

import com.taffygo.browser.ui.core.api.AssetProgressReport
import com.taffygo.browser.ui.core.api.ComposerCompletionReport
import com.taffygo.browser.ui.core.api.TaskAnswerReport
import com.taffygo.browser.ui.core.api.TaskArtifactExportReport
import org.chromium.mojo.system.MojoException
import org.chromium.taffy.core_api.mojom.TaffyProfileCoreApiObserver
import taffy.core_api.MAX_IDENTIFIER_BYTES
import taffy.core_api.MAX_TASK_ANSWER_DELTA_BYTES
import taffy.core_api.MAX_TASK_ARTIFACT_EXPORT_BYTES
import taffy.core_api.TaskArtifactKind

/** Maps the generated profile observer onto bounded UI-facing push channels. */
internal class ChromiumCoreApiObserver(
    private val acceptSnapshot: (Int, Long, Long, Int, ByteArray?) -> Unit,
    private val acceptPermissionRequest: (String, Int) -> Unit,
    private val pushes: CoreApiPushChannels,
    private val onInvalidTaskAnswer: () -> Unit,
    private val isClosed: () -> Boolean,
    private val onTransportFailure: () -> Unit,
) : TaffyProfileCoreApiObserver {
    override fun onSnapshot(
        availability: Int,
        serviceGeneration: Long,
        stateSequence: Long,
        statusSchemaVersion: Int,
        statusPayload: ByteArray?,
    ) = acceptSnapshot(
        availability,
        serviceGeneration,
        stateSequence,
        statusSchemaVersion,
        statusPayload,
    )

    override fun onPermissionRequest(requestId: String, permission: Int) =
        acceptPermissionRequest(requestId, permission)

    override fun onAssetProgress(
        assetId: String,
        assetRevision: String,
        writtenBytes: Long,
        totalBytes: Long,
    ) {
        // The next snapshot, not this short-lived progress figure, remains the
        // authority for the installed state.
        pushes.push(
            AssetProgressReport(
                assetId = assetId,
                assetRevision = assetRevision,
                writtenBytes = writtenBytes.toULong(),
                totalBytes = totalBytes.toULong(),
            ),
        )
    }

    override fun onComposerCompletion(requestId: String, text: String?) {
        // Null is the answer "no suggestion" and stays distinct from empty text.
        pushes.push(ComposerCompletionReport(requestId = requestId, text = text))
    }

    override fun onTaskAnswerDelta(
        taskId: String,
        callId: String,
        sequence: Int,
        text: String?,
        terminal: Boolean,
        complete: Boolean,
    ) {
        val valid = taskId.hasBoundedIdentifierBytes() &&
            callId.hasBoundedIdentifierBytes() &&
            (terminal == (text == null)) &&
            (!complete || terminal) &&
            (text == null || (
                text.isNotEmpty() &&
                    text.encodeToByteArray().size <= MAX_TASK_ANSWER_DELTA_BYTES
                ))
        if (!valid) {
            onInvalidTaskAnswer()
            return
        }
        pushes.push(
            TaskAnswerReport(
                taskId = taskId,
                callId = callId,
                sequence = sequence.toUInt(),
                text = text,
                terminal = terminal,
                complete = complete,
            ),
        )
    }

    override fun onTaskArtifactExport(
        requestId: String,
        taskId: String,
        artifactId: String,
        kind: Int,
        content: ByteArray,
    ) {
        val artifactKind = TaskArtifactKind.fromWire(kind.toUInt())
        if (!requestId.hasBoundedIdentifierBytes() ||
            !taskId.hasBoundedIdentifierBytes() ||
            !artifactId.hasBoundedIdentifierBytes() ||
            artifactKind == null ||
            content.isEmpty() ||
            content.size > MAX_TASK_ARTIFACT_EXPORT_BYTES
        ) {
            onInvalidTaskAnswer()
            return
        }
        pushes.push(
            TaskArtifactExportReport(
                requestId = requestId,
                taskId = taskId,
                artifactId = artifactId,
                kind = artifactKind,
                content = content,
            ),
        )
    }

    override fun onConnectionError(error: MojoException) {
        if (!isClosed()) onTransportFailure()
    }

    override fun close() = Unit
}

private fun String.hasBoundedIdentifierBytes(): Boolean =
    isNotEmpty() && encodeToByteArray().size <= MAX_IDENTIFIER_BYTES

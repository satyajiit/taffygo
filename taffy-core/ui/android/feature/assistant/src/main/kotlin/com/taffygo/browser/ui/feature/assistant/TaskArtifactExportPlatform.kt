// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.assistant

import android.app.Activity
import android.content.ContentResolver
import android.content.Context
import android.content.Intent
import android.net.Uri
import androidx.activity.result.contract.ActivityResultContract
import com.taffygo.browser.ui.core.task.TaskArtifactPayload
import com.taffygo.browser.ui.core.task.TaskArtifactProjection
import java.io.IOException
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.withContext

internal data class TaskArtifactDocumentSpec(
    val mimeType: String,
    val suggestedName: String,
)

/** Android chooses the destination; Taffy receives only the granted URI. */
internal class CreateTaskArtifactDocument :
    ActivityResultContract<TaskArtifactDocumentSpec, Uri?>() {
    override fun createIntent(context: Context, input: TaskArtifactDocumentSpec): Intent =
        Intent(Intent.ACTION_CREATE_DOCUMENT)
            .addCategory(Intent.CATEGORY_OPENABLE)
            .setType(input.mimeType)
            .putExtra(Intent.EXTRA_TITLE, input.suggestedName)

    override fun parseResult(resultCode: Int, intent: Intent?): Uri? =
        if (resultCode == Activity.RESULT_OK) intent?.data else null
}

/** Writes the exact validated core bytes without decoding or re-rendering. */
internal suspend fun writeTaskArtifact(
    resolver: ContentResolver,
    dispatcher: CoroutineDispatcher,
    uri: Uri,
    payload: TaskArtifactPayload,
): Boolean = withContext(dispatcher) {
    try {
        resolver.openOutputStream(uri, "w")?.use { output ->
            output.write(payload.copyContent())
            output.flush()
        } != null
    } catch (cancelled: CancellationException) {
        throw cancelled
    } catch (_: IOException) {
        false
    } catch (_: SecurityException) {
        false
    } catch (_: RuntimeException) {
        false
    }
}

/** Shares one private-cache copy through Chromium's non-exported FileProvider. */
internal suspend fun shareTaskArtifact(
    context: Context,
    dispatcher: CoroutineDispatcher,
    artifact: TaskArtifactProjection,
    payload: TaskArtifactPayload,
    chooserTitle: String,
): Boolean {
    val uri = withContext(dispatcher) {
        createPrivateExportShareUri(
            context = context,
            cacheDirectory = TASK_ARTIFACT_SHARE_DIRECTORY,
            suggestedName = taskArtifactShareName(artifact, payload),
            content = payload.copyContent(),
        )
    } ?: return false
    return launchPrivateExportChooser(
        context,
        artifact.mimeType,
        artifact.suggestedFileName,
        uri,
        chooserTitle,
    )
}

private fun taskArtifactShareName(
    artifact: TaskArtifactProjection,
    payload: TaskArtifactPayload,
): String {
    val requestName = payload.requestId
        .map { character -> if (character.isLetterOrDigit()) character else '_' }
        .joinToString(separator = "")
        .take(MAX_SHARE_REQUEST_NAME_CHARS)
    val extension = artifact.suggestedFileName.substringAfterLast('.', missingDelimiterValue = "bin")
    return "task-$requestName.$extension"
}

private const val TASK_ARTIFACT_SHARE_DIRECTORY = "pdfs/taffy-task-artifacts"
private const val MAX_SHARE_REQUEST_NAME_CHARS = 64

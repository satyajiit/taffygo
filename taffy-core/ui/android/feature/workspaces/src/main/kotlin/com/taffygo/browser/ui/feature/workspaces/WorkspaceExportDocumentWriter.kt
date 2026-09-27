// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.workspaces

import android.app.Activity
import android.content.ContentResolver
import android.content.Context
import android.content.Intent
import android.net.Uri
import androidx.activity.result.contract.ActivityResultContract
import com.taffygo.browser.ui.core.model.ExportFormat
import java.io.IOException
import java.io.OutputStream
import java.io.OutputStreamWriter
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.withContext

/** Fixed metadata for one trusted create-document request. */
internal data class ExportDocumentSpec(
    val mimeType: String,
    val suggestedName: String,
)

/** Safe names do not contain workspace or page text. */
internal fun ExportFormat.workspaceDocumentSpec(): ExportDocumentSpec = when (this) {
    ExportFormat.MARKDOWN -> ExportDocumentSpec(
        mimeType = "text/markdown",
        suggestedName = "taffy-workspace.md",
    )
    ExportFormat.COMMA_SEPARATED -> ExportDocumentSpec(
        mimeType = "text/csv",
        suggestedName = "taffy-workspace.csv",
    )
}

/** Library names reveal no collection or kept content. */
internal fun ExportFormat.libraryDocumentSpec(): ExportDocumentSpec = when (this) {
    ExportFormat.MARKDOWN -> ExportDocumentSpec(
        mimeType = "text/markdown",
        suggestedName = "taffy-library.md",
    )
    ExportFormat.COMMA_SEPARATED -> ExportDocumentSpec(
        mimeType = "text/csv",
        suggestedName = "taffy-library.csv",
    )
}

/** One typed Storage Access Framework create-document request. */
internal class CreateWorkspaceExportDocument :
    ActivityResultContract<ExportDocumentSpec, Uri?>() {
    override fun createIntent(context: Context, input: ExportDocumentSpec): Intent =
        Intent(Intent.ACTION_CREATE_DOCUMENT)
            .addCategory(Intent.CATEGORY_OPENABLE)
            .setType(input.mimeType)
            .putExtra(Intent.EXTRA_TITLE, input.suggestedName)

    override fun parseResult(resultCode: Int, intent: Intent?): Uri? =
        if (resultCode == Activity.RESULT_OK) intent?.data else null
}

/** Android's only ownership: write an already-rendered export to a granted URI. */
internal class WorkspaceExportDocumentWriter(
    private val resolver: ContentResolver,
    private val ioDispatcher: CoroutineDispatcher,
) {
    suspend fun write(uri: Uri, content: String): Boolean = withContext(ioDispatcher) {
        writeWorkspaceExport(content) { resolver.openOutputStream(uri, "wt") }
    }
}

/** Writes exact UTF-8 and contains document-provider failures at the trust boundary. */
internal fun writeWorkspaceExport(
    content: String,
    open: () -> OutputStream?,
): Boolean {
    return try {
        val output = open() ?: return false
        OutputStreamWriter(output, Charsets.UTF_8).use {
            it.write(content)
            it.flush()
        }
        true
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

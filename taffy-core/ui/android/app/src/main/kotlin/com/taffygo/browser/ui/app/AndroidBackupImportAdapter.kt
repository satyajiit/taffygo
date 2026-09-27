// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.app

import android.content.ContentResolver
import android.net.Uri
import android.os.ParcelFileDescriptor
import java.io.OutputStream
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.NonCancellable
import kotlinx.coroutines.currentCoroutineContext
import kotlinx.coroutines.ensureActive
import kotlinx.coroutines.withContext

/** Stages a person-selected encrypted archive without giving Android code a key or plaintext. */
class AndroidBackupImportAdapter(
    private val resolver: ContentResolver,
    private val ioDispatcher: CoroutineDispatcher,
    private val native: NativeImportStaging,
) {
    /**
     * Native validates the operation, authorized byte bound and retained
     * recovery authority, then returns a duplicated detached FD, or -1.
     * Inspection checks the exact file length, authenticates every section and
     * keeps a verified stage for review. It does not mutate the target profile.
     * Abandon is idempotent and nonthrowing, and wipes all operation stages/keys.
     * Methods run on ioDispatcher and must use native's sequenced storage owner.
     */
    interface NativeImportStaging {
        fun openEncryptedImportWriteFd(operationId: String, maxBytes: Long): Int
        fun inspectImportedArchive(operationId: String, actualBytes: Long): BackupDocumentImport.ImportStatus
        fun abandonImportStage(operationId: String)
    }

    private val transfer = BackupDocumentImport(object : BackupDocumentImport.Staging {
        override fun openEncryptedImport(operationId: String, maximumBytes: Long): OutputStream? =
            native.openEncryptedImportWriteFd(operationId, maximumBytes).takeIf { it >= 0 }?.let { fd ->
                ParcelFileDescriptor.AutoCloseOutputStream(ParcelFileDescriptor.adoptFd(fd))
            }

        override fun inspectImportedArchive(operationId: String, actualBytes: Long) =
            native.inspectImportedArchive(operationId, actualBytes)

        override fun abandon(operationId: String) = native.abandonImportStage(operationId)
    })

    suspend fun copyAndInspect(
        operationId: String,
        maximumArchiveBytes: Long,
        uri: Uri,
    ): BackupDocumentImport.ImportStatus = try {
        withContext(ioDispatcher) {
            val context = currentCoroutineContext()
            val document = BackupDocumentImport.Document {
                if (uri.scheme != ContentResolver.SCHEME_CONTENT) {
                    throw SecurityException("Backup requires a granted document URI")
                }
                resolver.openInputStream(uri)
            }
            transfer.copyAndInspect(operationId, maximumArchiveBytes, document) { context.ensureActive() }
        }
    } catch (cancelled: CancellationException) {
        // Prompt cancellation can discard a VERIFIED result on the return to
        // the calling dispatcher, after the stream helper has retained it.
        // The caller never received its review authority, so release it here.
        if (operationId.isNotBlank()) {
            withContext(NonCancellable + ioDispatcher) { native.abandonImportStage(operationId) }
        }
        throw cancelled
    }

    suspend fun abandon(operationId: String) {
        if (operationId.isNotBlank()) {
            withContext(NonCancellable + ioDispatcher) { native.abandonImportStage(operationId) }
        }
    }
}

// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.app

import android.content.ContentResolver
import android.net.Uri
import android.os.ParcelFileDescriptor
import android.provider.DocumentsContract
import android.provider.OpenableColumns
import java.io.InputStream
import java.io.OutputStream
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.currentCoroutineContext
import kotlinx.coroutines.ensureActive
import kotlinx.coroutines.withContext

/** Android document access for one native-owned, already encrypted archive operation. */
class AndroidBackupDocumentAdapter(
    private val resolver: ContentResolver,
    private val ioDispatcher: CoroutineDispatcher,
    native: NativeStaging,
) {
    /**
     * Native returns duplicated, detached FDs, or -1 on refusal. It checks
     * operation lifetime and the exact byte count before opening either file.
     * Methods run on ioDispatcher; native must use its sequenced storage owner.
     * No profile path, plaintext, manifest or recovery key crosses this seam.
     */
    interface NativeStaging {
        fun openEncryptedArchiveReadFd(operationId: String): Int
        fun openReadbackWriteFd(operationId: String, expectedArchiveBytes: Long): Int
        fun verifyEncryptedReadback(operationId: String): BackupDocumentTransfer.ReadbackStatus
        fun abandonBackupStage(operationId: String)
    }

    private val transfer = BackupDocumentTransfer(object : BackupDocumentTransfer.Staging {
        override fun openArchive(operationId: String): InputStream? =
            native.openEncryptedArchiveReadFd(operationId).takeIf { it >= 0 }?.let { fd ->
                ParcelFileDescriptor.AutoCloseInputStream(ParcelFileDescriptor.adoptFd(fd))
            }

        override fun openReadback(operationId: String, expectedBytes: Long): OutputStream? =
            native.openReadbackWriteFd(operationId, expectedBytes).takeIf { it >= 0 }?.let { fd ->
                ParcelFileDescriptor.AutoCloseOutputStream(ParcelFileDescriptor.adoptFd(fd))
            }

        override fun verifyReadback(operationId: String) = native.verifyEncryptedReadback(operationId)

        override fun abandon(operationId: String) = native.abandonBackupStage(operationId)
    })

    suspend fun writeAndVerify(
        operationId: String,
        expectedArchiveBytes: Long,
        uri: Uri,
    ): BackupDocumentTransfer.WriteResult = withContext(ioDispatcher) {
        val context = currentCoroutineContext()
        transfer.writeAndVerify(operationId, expectedArchiveBytes, document(uri)) { context.ensureActive() }
    }

    suspend fun deleteAndVerify(
        uri: Uri,
        mayDispatch: () -> Boolean,
    ): BackupDocumentTransfer.DeleteResult = withContext(ioDispatcher) {
        currentCoroutineContext().ensureActive()
        if (mayDispatch()) BackupDocumentTransfer.deleteAndVerify(document(uri))
        else BackupDocumentTransfer.DeleteResult.UNVERIFIABLE
    }

    /** A bounded label for a fresh destructive review, not evidence that this is a valid backup. */
    suspend fun readDeletionDisplayName(uri: Uri, mayRead: () -> Boolean): String? = withContext(ioDispatcher) {
        currentCoroutineContext().ensureActive()
        if (uri.scheme != ContentResolver.SCHEME_CONTENT || !mayRead()) return@withContext null
        try {
            // The four-argument form, which is the one a documents provider
            // actually answers. ContentResolver turns the older five-argument
            // call into exactly this before it crosses the binder, and
            // DocumentsProvider's five-argument override exists only to refuse
            // it: "Pre-Android-O query format not supported." A device never
            // reaches that refusal, so the older call was correct there and
            // untestable everywhere else, because Robolectric's resolver hands
            // the provider the five-argument form the platform never sends.
            resolver.query(uri, arrayOf(OpenableColumns.DISPLAY_NAME), null, null)?.use { cursor ->
                val column = cursor.getColumnIndex(OpenableColumns.DISPLAY_NAME)
                if (column < 0 || !cursor.moveToFirst() || cursor.isNull(column)) null
                else cursor.getString(column)?.takeIf(::isBackupDocumentNameSafe)
            }
        } catch (_: RuntimeException) {
            null
        }
    }

    private fun document(uri: Uri): BackupDocumentTransfer.Document = object : BackupDocumentTransfer.Document {
        private fun requireGrantedDocument() {
            if (uri.scheme != ContentResolver.SCHEME_CONTENT) {
                throw SecurityException("Backup requires a granted document URI")
            }
        }

        override fun openWriter(): OutputStream? {
            requireGrantedDocument()
            return resolver.openOutputStream(uri, "wt")
        }

        override fun openReader(): InputStream? {
            requireGrantedDocument()
            return resolver.openInputStream(uri)
        }

        override fun delete(): Boolean {
            requireGrantedDocument()
            return DocumentsContract.deleteDocument(resolver, uri)
        }
    }
}

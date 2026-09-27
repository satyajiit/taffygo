// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.app

import java.io.IOException
import java.io.InputStream
import java.io.OutputStream
import kotlinx.coroutines.CancellationException

/** Copies encrypted bytes only. Native authenticates and retains a verified restore stage. */
class BackupDocumentImport(private val staging: Staging) {
    interface Staging {
        fun openEncryptedImport(operationId: String, maximumBytes: Long): OutputStream?
        fun inspectImportedArchive(operationId: String, actualBytes: Long): ImportStatus
        fun abandon(operationId: String)
    }

    fun interface Document {
        fun openReader(): InputStream?
    }

    enum class ImportStatus { VERIFIED, REFUSED, UNAVAILABLE }

    /**
     * The native operation authorizes [maximumBytes] before the provider is
     * opened. Provider metadata is not trusted, and no header is parsed here.
     * VERIFIED retains a native stage for later review; it commits no records.
     * The owner must explicitly restore or abandon that operation afterwards.
     */
    fun copyAndInspect(
        operationId: String,
        maximumBytes: Long,
        document: Document,
        ensureActive: () -> Unit,
    ): ImportStatus {
        if (operationId.isBlank()) return ImportStatus.UNAVAILABLE
        var retained = false
        try {
            ensureActive()
            if (maximumBytes <= 0) return ImportStatus.REFUSED
            val output = staging.openEncryptedImport(operationId, maximumBytes)
                ?: return ImportStatus.UNAVAILABLE
            val copied = output.use { destination ->
                val input = document.openReader() ?: return ImportStatus.UNAVAILABLE
                input.use { source -> copyBounded(source, destination, maximumBytes, ensureActive) }
            }
            if (copied == null || copied == 0L) return ImportStatus.REFUSED
            // Both sides must be closed, including a provider-backed input pipe,
            // before native examines the exact file length and authenticates it.
            ensureActive()
            val status = staging.inspectImportedArchive(operationId, copied)
            ensureActive()
            retained = status == ImportStatus.VERIFIED
            return status
        } catch (cancelled: CancellationException) {
            throw cancelled
        } catch (_: IOException) {
            return ImportStatus.UNAVAILABLE
        } catch (_: RuntimeException) {
            return ImportStatus.UNAVAILABLE
        } finally {
            if (!retained) staging.abandon(operationId)
        }
    }
}

private fun copyBounded(
    input: InputStream,
    output: OutputStream,
    maximumBytes: Long,
    ensureActive: () -> Unit,
): Long? {
    val buffer = ByteArray(64 * 1024)
    var copied = 0L
    while (copied < maximumBytes) {
        ensureActive()
        val count = input.read(buffer, 0, minOf(maximumBytes - copied, buffer.size.toLong()).toInt())
        if (count < 0) {
            output.flush()
            return copied
        }
        if (count == 0) throw IOException("Encrypted backup input made no progress")
        output.write(buffer, 0, count)
        copied += count
    }
    ensureActive()
    if (input.read() != -1) return null
    output.flush()
    return copied
}

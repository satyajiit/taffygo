// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.app

import java.io.FileNotFoundException
import java.io.IOException
import java.io.InputStream
import java.io.OutputStream
import kotlinx.coroutines.CancellationException

/** Physical encrypted-byte transfer; archive meaning and authentication belong to native code. */
class BackupDocumentTransfer(private val staging: Staging) {
    interface Staging {
        fun openArchive(operationId: String): InputStream?
        fun openReadback(operationId: String, expectedBytes: Long): OutputStream?
        fun verifyReadback(operationId: String): ReadbackStatus
        fun abandon(operationId: String)
    }

    interface Document {
        fun openWriter(): OutputStream?
        fun openReader(): InputStream?
        fun delete(): Boolean
    }

    enum class ReadbackStatus { VERIFIED, REFUSED, UNAVAILABLE }
    enum class WriteResult { VERIFIED, INCOMPLETE, UNAVAILABLE }
    enum class DeleteResult { DELETED, STILL_PRESENT, UNVERIFIABLE }

    /**
     * Native validates the exact planned byte count when opening readback,
     * before the provider is opened for writing. Only encrypted bytes cross
     * either stream. Failure leaves the provider document for explicit cleanup.
     * Every native stage is released, even after cancellation or verification.
     */
    fun writeAndVerify(
        operationId: String,
        expectedBytes: Long,
        document: Document,
        ensureActive: () -> Unit,
    ): WriteResult {
        if (operationId.isBlank()) return WriteResult.UNAVAILABLE
        var destinationOpened = false
        try {
            ensureActive()
            if (expectedBytes <= 0) return WriteResult.UNAVAILABLE
            val readback = staging.openReadback(operationId, expectedBytes)
                ?: return WriteResult.UNAVAILABLE
            readback.use { readbackOutput ->
                val source = staging.openArchive(operationId) ?: return WriteResult.UNAVAILABLE
                source.use { archive ->
                    val destination = document.openWriter() ?: return WriteResult.UNAVAILABLE
                    destinationOpened = true
                    destination.use { output -> copyExactly(archive, output, expectedBytes, ensureActive) }
                    // Closing the document before reopening also works with
                    // providers whose output is a pipe committed only at close.
                }
                val reopened = document.openReader() ?: return WriteResult.INCOMPLETE
                reopened.use { input -> copyExactly(input, readbackOutput, expectedBytes, ensureActive) }
            }
            ensureActive()
            return when (staging.verifyReadback(operationId)) {
                ReadbackStatus.VERIFIED -> WriteResult.VERIFIED
                ReadbackStatus.REFUSED, ReadbackStatus.UNAVAILABLE -> WriteResult.INCOMPLETE
            }
        } catch (cancelled: CancellationException) {
            throw cancelled
        } catch (_: IOException) {
            return if (destinationOpened) WriteResult.INCOMPLETE else WriteResult.UNAVAILABLE
        } catch (_: RuntimeException) {
            return if (destinationOpened) WriteResult.INCOMPLETE else WriteResult.UNAVAILABLE
        } finally {
            staging.abandon(operationId)
        }
    }

    companion object {
        /** Permission loss and provider failure do not prove that a copy is absent. */
        fun deleteAndVerify(document: Document): DeleteResult = try {
            if (!document.delete()) {
                DeleteResult.UNVERIFIABLE
            } else {
                try {
                    document.openReader()?.use { DeleteResult.STILL_PRESENT }
                        ?: DeleteResult.UNVERIFIABLE
                } catch (_: FileNotFoundException) {
                    DeleteResult.DELETED
                }
            }
        } catch (cancelled: CancellationException) {
            throw cancelled
        } catch (_: IOException) {
            DeleteResult.UNVERIFIABLE
        } catch (_: RuntimeException) {
            DeleteResult.UNVERIFIABLE
        }
    }
}

private fun copyExactly(
    input: InputStream,
    output: OutputStream,
    expectedBytes: Long,
    ensureActive: () -> Unit,
) {
    val buffer = ByteArray(64 * 1024)
    var remaining = expectedBytes
    while (remaining > 0) {
        ensureActive()
        val count = input.read(buffer, 0, minOf(remaining, buffer.size.toLong()).toInt())
        if (count <= 0) throw IOException("Incomplete encrypted backup stream")
        output.write(buffer, 0, count)
        remaining -= count
    }
    ensureActive()
    if (input.read() != -1) throw IOException("Trailing encrypted backup bytes")
    output.flush()
}

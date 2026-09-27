// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.app

import java.io.ByteArrayInputStream
import java.io.ByteArrayOutputStream
import java.io.IOException
import java.io.InputStream
import java.io.OutputStream
import kotlinx.coroutines.CancellationException
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Assert.fail
import org.junit.Test

class BackupDocumentTransferTest {
    private val bytes = ByteArray(150_000) { (it % 251).toByte() }
    private val staging = TestStaging(bytes)
    private val document = TestDocument()
    private val transfer = BackupDocumentTransfer(staging)

    @Test
    fun `success requires exact closed document readback and native verification`() {
        assertEquals(BackupDocumentTransfer.WriteResult.VERIFIED, write())
        assertArrayEquals(bytes, document.stored)
        assertArrayEquals(bytes, staging.readback.toByteArray())
        assertEquals(1, staging.verifications)
        assertEquals(listOf("operation"), staging.abandoned)
        assertTrue(staging.sourceClosed)
        assertTrue(staging.readbackClosed)
        assertTrue(document.readerClosed)
        assertTrue(document.largestWrite <= 64 * 1024)
    }

    @Test
    fun `native length refusal precedes every provider write`() {
        staging.readbackAvailable = false
        assertEquals(BackupDocumentTransfer.WriteResult.UNAVAILABLE, write())
        assertFalse(document.writerOpened)
        assertEquals(0, staging.verifications)
        assertEquals(listOf("operation"), staging.abandoned)
    }

    @Test
    fun `short and trailing source streams cannot be reported as written`() {
        for (expected in listOf(bytes.size - 1L, bytes.size + 1L)) {
            assertEquals(BackupDocumentTransfer.WriteResult.INCOMPLETE, write(expected))
        }
        assertEquals(0, staging.verifications)
        assertTrue(staging.sourceClosed)
        assertTrue(staging.readbackClosed)
    }

    @Test
    fun `short and trailing provider readbacks never reach verification`() {
        for (changed in listOf(bytes.copyOf(bytes.size - 1), bytes + byteArrayOf(1))) {
            document.readbackOverride = changed
            assertEquals(BackupDocumentTransfer.WriteResult.INCOMPLETE, write())
        }
        assertEquals(0, staging.verifications)
        assertTrue(document.readerClosed)
    }

    @Test
    fun `same length corruption is refused by native authentication`() {
        document.readbackOverride = bytes.copyOf().also { it[0] = (it[0] + 1).toByte() }
        assertEquals(BackupDocumentTransfer.WriteResult.INCOMPLETE, write())
        assertEquals(1, staging.verifications)
        assertEquals(listOf("operation"), staging.abandoned)
    }

    @Test
    fun `provider write and close failures leave incomplete results`() {
        document.writeFailure = true
        assertEquals(BackupDocumentTransfer.WriteResult.INCOMPLETE, write())
        document.writeFailure = false
        document.closeFailure = true
        assertEquals(BackupDocumentTransfer.WriteResult.INCOMPLETE, write())
        assertEquals(0, staging.verifications)
    }

    @Test
    fun `provider reopen permission loss is not success`() {
        document.readFailure = SecurityException("permission revoked")
        assertEquals(BackupDocumentTransfer.WriteResult.INCOMPLETE, write())
        assertEquals(0, staging.verifications)
    }

    @Test
    fun `missing provider streams are refused without authenticating`() {
        document.writerAvailable = false
        assertEquals(BackupDocumentTransfer.WriteResult.UNAVAILABLE, write())
        document.writerAvailable = true
        document.readerAvailable = false
        assertEquals(BackupDocumentTransfer.WriteResult.INCOMPLETE, write())
        assertEquals(0, staging.verifications)
    }

    @Test
    fun `cancellation closes owned streams and releases the native stage`() {
        var checks = 0
        try {
            transfer.writeAndVerify("operation", bytes.size.toLong(), document) {
                if (++checks == 3) throw CancellationException("cancel")
            }
            fail("Cancellation must reach the caller")
        } catch (_: CancellationException) {
            assertTrue(staging.sourceClosed)
            assertTrue(staging.readbackClosed)
            assertEquals(listOf("operation"), staging.abandoned)
            assertEquals(0, staging.verifications)
        }
    }

    @Test
    fun `zero and negative lengths cannot open a destination`() {
        for (expected in listOf(0L, -1L, Long.MIN_VALUE)) {
            assertEquals(BackupDocumentTransfer.WriteResult.UNAVAILABLE, write(expected))
        }
        assertFalse(document.writerOpened)
        assertEquals(3, staging.abandoned.size)
    }

    private fun write(expected: Long = bytes.size.toLong()) =
        transfer.writeAndVerify("operation", expected, document) {}

    private class TestStaging(private val bytes: ByteArray) : BackupDocumentTransfer.Staging {
        var readbackAvailable = true
        var sourceClosed = false
        var readbackClosed = false
        var verifications = 0
        val abandoned = mutableListOf<String>()
        val readback = object : ByteArrayOutputStream() {
            override fun close() { readbackClosed = true }
        }

        override fun openArchive(operationId: String): InputStream =
            object : ByteArrayInputStream(bytes) {
                override fun close() { sourceClosed = true }
            }

        override fun openReadback(operationId: String, expectedBytes: Long): OutputStream? {
            readback.reset()
            return readback.takeIf { readbackAvailable }
        }

        override fun verifyReadback(operationId: String): BackupDocumentTransfer.ReadbackStatus {
            assertTrue(sourceClosed)
            assertTrue(readbackClosed)
            verifications++
            return if (bytes.contentEquals(readback.toByteArray())) {
                BackupDocumentTransfer.ReadbackStatus.VERIFIED
            } else {
                BackupDocumentTransfer.ReadbackStatus.REFUSED
            }
        }

        override fun abandon(operationId: String) { abandoned += operationId }
    }

    private class TestDocument : BackupDocumentTransfer.Document {
        var stored = byteArrayOf()
        var readbackOverride: ByteArray? = null
        var writeFailure = false
        var closeFailure = false
        var readFailure: RuntimeException? = null
        var writerAvailable = true
        var readerAvailable = true
        var writerOpened = false
        var writerClosed = false
        var readerClosed = false
        var largestWrite = 0

        override fun openWriter(): OutputStream? {
            if (!writerAvailable) return null
            writerOpened = true
            return object : ByteArrayOutputStream() {
                override fun write(buffer: ByteArray, offset: Int, length: Int) {
                    largestWrite = maxOf(largestWrite, length)
                    if (writeFailure) throw IOException("provider write failed")
                    super.write(buffer, offset, length)
                }
                override fun close() {
                    writerClosed = true
                    if (closeFailure) throw IOException("provider close failed")
                    stored = toByteArray()
                }
            }
        }

        override fun openReader(): InputStream? {
            assertTrue(writerClosed)
            readFailure?.let { throw it }
            if (!readerAvailable) return null
            return object : ByteArrayInputStream(readbackOverride ?: stored) {
                override fun close() { readerClosed = true }
            }
        }

        override fun delete(): Boolean = throw UnsupportedOperationException("explicit cleanup only")
    }
}

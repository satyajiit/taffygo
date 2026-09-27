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
import org.junit.Assert.assertThrows
import org.junit.Assert.assertTrue
import org.junit.Test

class BackupDocumentImportTest {
    private val bytes = byteArrayOf(11, 29, 37, 53)
    private val operationId = "restore-operation"

    @Test
    fun `unknown length source is closed before native inspects and verified stage is retained`() {
        val fixture = Fixture(bytes)

        assertEquals(verified, fixture.run(maximumBytes = 100))
        assertArrayEquals(bytes, fixture.output.toByteArray())
        assertEquals(bytes.size.toLong(), fixture.inspectedBytes)
        assertTrue(fixture.inputClosed)
        assertTrue(fixture.outputClosed)
        assertEquals(0, fixture.abandoned)
    }

    @Test
    fun `exact maximum bytes require an observed EOF and may be retained`() {
        val fixture = Fixture(bytes)

        assertEquals(verified, fixture.run(maximumBytes = bytes.size.toLong()))
        assertEquals(bytes.size.toLong(), fixture.inspectedBytes)
        assertEquals(0, fixture.abandoned)
    }

    @Test
    fun `one byte over the bound is never written or authenticated`() {
        val fixture = Fixture(bytes + 71)

        assertEquals(refused, fixture.run(maximumBytes = bytes.size.toLong()))
        assertArrayEquals(bytes, fixture.output.toByteArray())
        assertEquals(null, fixture.inspectedBytes)
        assertEquals(1, fixture.abandoned)
        assertTrue(fixture.inputClosed)
        assertTrue(fixture.outputClosed)
    }

    @Test
    fun `empty input cannot become an authenticated archive`() {
        val fixture = Fixture(byteArrayOf())

        assertEquals(refused, fixture.run())
        assertEquals(null, fixture.inspectedBytes)
        assertEquals(1, fixture.abandoned)
    }

    @Test
    fun `native must authorize the bound before a document is opened`() {
        val fixture = Fixture(bytes).apply { nativeAccepts = false }

        assertEquals(unavailable, fixture.run())
        assertFalse(fixture.inputOpened)
        assertEquals(null, fixture.inspectedBytes)
        assertEquals(1, fixture.abandoned)
    }

    @Test
    fun `nonpositive bounds and blank operations never open either side`() {
        for (maximum in listOf(0L, -1L, Long.MIN_VALUE)) {
            val fixture = Fixture(bytes)
            assertEquals(refused, fixture.run(maximumBytes = maximum))
            assertFalse(fixture.outputOpened)
            assertFalse(fixture.inputOpened)
            assertEquals(1, fixture.abandoned)
        }
        val fixture = Fixture(bytes)
        assertEquals(unavailable, fixture.run(id = " "))
        assertFalse(fixture.outputOpened)
        assertEquals(0, fixture.abandoned)
    }

    @Test
    fun `large native bound does not allocate proportionally or trust available`() {
        val content = ByteArray(160_001) { (it % 251).toByte() }
        val fixture = Fixture(content)

        assertEquals(verified, fixture.run(maximumBytes = Long.MAX_VALUE))
        assertArrayEquals(content, fixture.output.toByteArray())
        assertTrue(fixture.maximumRead <= 64 * 1024)
        assertEquals(content.size.toLong(), fixture.inspectedBytes)
    }

    @Test
    fun `provider read failure cannot authenticate a partially copied archive`() {
        val fixture = Fixture(bytes).apply { failRead = true }

        assertEquals(unavailable, fixture.run())
        assertEquals(null, fixture.inspectedBytes)
        assertEquals(1, fixture.abandoned)
        assertTrue(fixture.outputClosed)
    }

    @Test
    fun `zero progress is unavailable instead of an unbounded copy loop`() {
        val fixture = Fixture(bytes).apply { zeroRead = true }

        assertEquals(unavailable, fixture.run())
        assertEquals(null, fixture.inspectedBytes)
        assertEquals(1, fixture.abandoned)
    }

    @Test
    fun `missing document and revoked access close and abandon the native stage`() {
        for (revoked in listOf(false, true)) {
            val fixture = Fixture(bytes).apply {
                missingDocument = !revoked
                permissionRevoked = revoked
            }
            assertEquals(unavailable, fixture.run())
            assertEquals(null, fixture.inspectedBytes)
            assertEquals(1, fixture.abandoned)
            assertTrue(fixture.outputClosed)
        }
    }

    @Test
    fun `write flush and close failures cannot retain an archive`() {
        for (failure in listOf("write", "flush", "input-close", "output-close")) {
            val fixture = Fixture(bytes).apply { failAt = failure }
            assertEquals(unavailable, fixture.run())
            assertEquals(null, fixture.inspectedBytes)
            assertEquals(1, fixture.abandoned)
            assertTrue(fixture.inputClosed)
            assertTrue(fixture.outputClosed)
        }
    }

    @Test
    fun `authentication refusal and native unavailability never retain stages`() {
        for (status in listOf(refused, unavailable)) {
            val fixture = Fixture(bytes).apply { nativeStatus = status }
            assertEquals(status, fixture.run())
            assertEquals(bytes.size.toLong(), fixture.inspectedBytes)
            assertEquals(1, fixture.abandoned)
        }
    }

    @Test
    fun `cancellation before open and during copy releases the operation`() {
        for (cancelAt in listOf(1, 3)) {
            val fixture = Fixture(bytes)
            var checkpoints = 0
            assertThrows(CancellationException::class.java) {
                fixture.run {
                    if (++checkpoints == cancelAt) throw CancellationException()
                }
            }
            assertEquals(null, fixture.inspectedBytes)
            assertEquals(1, fixture.abandoned)
            if (fixture.outputOpened) assertTrue(fixture.outputClosed)
            if (fixture.inputOpened) assertTrue(fixture.inputClosed)
        }
    }

    @Test
    fun `cancellation while native authenticates abandons even a verified stage`() {
        val fixture = Fixture(bytes)
        assertThrows(CancellationException::class.java) {
            fixture.run {
                if (fixture.inspectedBytes != null) throw CancellationException()
            }
        }
        assertEquals(bytes.size.toLong(), fixture.inspectedBytes)
        assertEquals(1, fixture.abandoned)
    }

    private inner class Fixture(content: ByteArray) : BackupDocumentImport.Staging {
        val output = ByteArrayOutputStream()
        private val source = ByteArrayInputStream(content)
        var inputOpened = false
        var inputClosed = false
        var outputOpened = false
        var outputClosed = false
        var inspectedBytes: Long? = null
        var abandoned = 0
        var maximumRead = 0
        var nativeAccepts = true
        var nativeStatus = verified
        var failRead = false
        var zeroRead = false
        var missingDocument = false
        var permissionRevoked = false
        var failAt = ""

        override fun openEncryptedImport(operationId: String, maximumBytes: Long): OutputStream? {
            assertEquals(this@BackupDocumentImportTest.operationId, operationId)
            assertFalse(inputOpened)
            if (!nativeAccepts) return null
            outputOpened = true
            return object : OutputStream() {
                override fun write(value: Int) {
                    if (failAt == "write") throw IOException()
                    output.write(value)
                }
                override fun flush() {
                    if (failAt == "flush") throw IOException()
                }
                override fun close() {
                    outputClosed = true
                    if (failAt == "output-close") throw IOException()
                }
            }
        }

        override fun inspectImportedArchive(operationId: String, actualBytes: Long): BackupDocumentImport.ImportStatus {
            assertEquals(this@BackupDocumentImportTest.operationId, operationId)
            assertTrue(inputClosed)
            assertTrue(outputClosed)
            inspectedBytes = actualBytes
            return nativeStatus
        }

        override fun abandon(operationId: String) {
            assertEquals(this@BackupDocumentImportTest.operationId, operationId)
            abandoned++
        }

        fun run(
            maximumBytes: Long = 100,
            id: String = operationId,
            ensureActive: () -> Unit = {},
        ): BackupDocumentImport.ImportStatus {
            val document = BackupDocumentImport.Document {
                if (permissionRevoked) throw SecurityException()
                if (missingDocument) return@Document null
                inputOpened = true
                object : InputStream() {
                    override fun read(): Int = source.read()
                    override fun available(): Int = throw AssertionError("Provider size is untrusted")
                    override fun read(buffer: ByteArray, offset: Int, length: Int): Int {
                        maximumRead = maxOf(maximumRead, length)
                        if (failRead) throw IOException()
                        if (zeroRead) return 0
                        return source.read(buffer, offset, length)
                    }
                    override fun close() {
                        inputClosed = true
                        if (failAt == "input-close") throw IOException()
                    }
                }
            }
            return BackupDocumentImport(this).copyAndInspect(id, maximumBytes, document, ensureActive)
        }
    }

    private companion object {
        val verified = BackupDocumentImport.ImportStatus.VERIFIED
        val refused = BackupDocumentImport.ImportStatus.REFUSED
        val unavailable = BackupDocumentImport.ImportStatus.UNAVAILABLE
    }
}

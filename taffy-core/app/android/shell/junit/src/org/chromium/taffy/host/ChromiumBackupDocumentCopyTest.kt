// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.host

import android.content.Context
import android.content.pm.ProviderInfo
import android.database.Cursor
import android.net.Uri
import android.os.ParcelFileDescriptor
import android.provider.DocumentsContract
import com.taffygo.browser.ui.app.BackupDocumentTransfer
import com.taffygo.browser.ui.app.BackupRecoveryKeySession
import com.taffygo.browser.ui.app.BackupWindowHost
import java.io.File
import java.util.concurrent.Executors
import kotlin.coroutines.CoroutineContext
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.asCoroutineDispatcher
import kotlinx.coroutines.async
import kotlinx.coroutines.runBlocking
import org.chromium.base.TestDocumentsProvider
import org.chromium.base.test.BaseRobolectricTestRunner
import org.chromium.taffy.browser.TaffyBackupWorkflowBridge
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RuntimeEnvironment
import org.robolectric.shadows.ShadowContentResolver

@RunWith(BaseRobolectricTestRunner::class)
class ChromiumBackupDocumentCopyTest {
    @Test
    fun `selected old backup mints one exact named deletion capability`() {
        val fixture = fixture(Dispatchers.Unconfined, TaffyBackupWorkflowBridge.ACCEPTED)
        try {
            val request = requireNotNull(fixture.host.openBackupDeletionRequest())
            val copy = requireNotNull(
                runBlocking {
                    fixture.host.completeBackupDeletionSelection(request, fixture.destinationUri)
                },
            )

            assertEquals(fixture.destination.name, copy.displayName)
            assertNull(
                runBlocking {
                    fixture.host.completeBackupDeletionSelection(request, fixture.destinationUri)
                },
            )
            assertEquals(
                BackupDocumentTransfer.DeleteResult.DELETED,
                runBlocking { copy.deleteAndVerify() },
            )
            assertEquals(1, fixture.provider.deleteCalls)
        } finally {
            fixture.close()
        }
    }

    @Test
    fun `closing a queued selection prevents metadata IO and preserves a newer request`() {
        val io = HoldingDispatcher()
        val fixture = fixture(io, TaffyBackupWorkflowBridge.ACCEPTED)
        try {
            val old = requireNotNull(fixture.host.openBackupDeletionRequest())
            val readsBefore = fixture.provider.queryCalls
            io.hold()
            val pending = CoroutineScope(Dispatchers.Unconfined).async {
                fixture.host.completeBackupDeletionSelection(old, fixture.destinationUri)
            }
            assertFalse(pending.isCompleted)
            old.close()
            val current = requireNotNull(fixture.host.openBackupDeletionRequest())
            io.runHeld()

            assertNull(runBlocking { pending.await() })
            assertEquals(readsBefore, fixture.provider.queryCalls)
            assertEquals(0, fixture.provider.deleteCalls)
            assertNull(fixture.host.openBackupDeletionRequest())
            val copy = requireNotNull(runBlocking {
                fixture.host.completeBackupDeletionSelection(current, fixture.destinationUri)
            })
            assertEquals(fixture.destination.name, copy.displayName)
            assertEquals(readsBefore + 1, fixture.provider.queryCalls)
            copy.close()
            assertEquals(0, fixture.provider.deleteCalls)
            assertTrue(fixture.destination.exists())
        } finally {
            fixture.close()
        }
    }

    @Test
    fun `window close before queued metadata dispatch prevents any query or copy`() {
        val io = HoldingDispatcher()
        val fixture = fixture(io, TaffyBackupWorkflowBridge.ACCEPTED)
        try {
            val request = requireNotNull(fixture.host.openBackupDeletionRequest())
            val readsBefore = fixture.provider.queryCalls
            io.hold()
            val pending = CoroutineScope(Dispatchers.Unconfined).async {
                fixture.host.completeBackupDeletionSelection(request, fixture.destinationUri)
            }
            assertFalse(pending.isCompleted)
            fixture.host.close()
            io.runHeld()

            assertNull(runBlocking { pending.await() })
            assertEquals(readsBefore, fixture.provider.queryCalls)
            assertEquals(0, fixture.provider.deleteCalls)
            assertTrue(fixture.destination.exists())
            assertNull(fixture.host.openBackupDeletionRequest())
        } finally {
            fixture.close()
        }
    }

    @Test
    fun `incomplete export keeps one exact cleanup after native withdrawal retry`() {
        val executor = Executors.newSingleThreadExecutor { Thread(it, "backup-copy-io") }
        val io = executor.asCoroutineDispatcher()
        val fixture = fixture(io, TaffyBackupWorkflowBridge.REFUSED, failFirstAbandon = true)
        try {
            val result = fixture.export()

            assertEquals(BackupDocumentTransfer.WriteResult.INCOMPLETE, result.status)
            val copy = requireNotNull(result.copy)
            assertEquals(2, fixture.native.abandonAttempts)
            assertEquals(
                BackupDocumentTransfer.DeleteResult.DELETED,
                runBlocking { copy.deleteAndVerify() },
            )
            // Thread.currentThread().name, not a bare executor name: kotlinx
            // .coroutines decorates the thread it is running on with a
            // " @coroutine#n" suffix, so the exact comparison this once made
            // could never hold. The subject is which executor the delete was
            // dispatched on, and the prefix is what carries it.
            assertTrue(
                requireNotNull(fixture.provider.deleteThread).startsWith("backup-copy-io"),
            )
            assertEquals(
                BackupDocumentTransfer.DeleteResult.UNVERIFIABLE,
                runBlocking { copy.deleteAndVerify() },
            )
            assertEquals(1, fixture.provider.deleteCalls)
        } finally {
            fixture.close()
            io.close()
        }
    }

    @Test
    fun `queued delete cannot revive after deactivate and reactivate`() {
        val io = HoldingDispatcher()
        val fixture = fixture(io, TaffyBackupWorkflowBridge.ACCEPTED)
        try {
            val copy = requireNotNull(fixture.export().copy)
            io.hold()
            val pending = CoroutineScope(Dispatchers.Unconfined).async { copy.deleteAndVerify() }
            assertFalse(pending.isCompleted)

            fixture.host.deactivate()
            assertTrue(fixture.host.activate())
            io.runHeld()

            assertEquals(
                BackupDocumentTransfer.DeleteResult.UNVERIFIABLE,
                runBlocking { pending.await() },
            )
            assertEquals(0, fixture.provider.deleteCalls)
            assertTrue(fixture.destination.exists())
        } finally {
            fixture.close()
        }
    }

    @Test
    fun `window close erases an undispatched copy without deleting it`() {
        val fixture = fixture(Dispatchers.Unconfined, TaffyBackupWorkflowBridge.ACCEPTED)
        try {
            val copy = requireNotNull(fixture.export().copy)
            fixture.host.close()

            assertFalse(fixture.host.activate())
            assertEquals(
                BackupDocumentTransfer.DeleteResult.UNVERIFIABLE,
                runBlocking { copy.deleteAndVerify() },
            )
            assertEquals(0, fixture.provider.deleteCalls)
            assertTrue(fixture.destination.exists())
        } finally {
            fixture.close()
        }
    }

    @Test
    fun `window close while delete is queued prevents provider dispatch`() {
        val io = HoldingDispatcher()
        val fixture = fixture(io, TaffyBackupWorkflowBridge.ACCEPTED)
        try {
            val copy = requireNotNull(fixture.export().copy)
            io.hold()
            val pending = CoroutineScope(Dispatchers.Unconfined).async { copy.deleteAndVerify() }
            fixture.host.close()
            io.runHeld()

            assertEquals(
                BackupDocumentTransfer.DeleteResult.UNVERIFIABLE,
                runBlocking { pending.await() },
            )
            assertEquals(0, fixture.provider.deleteCalls)
            assertTrue(fixture.destination.exists())
        } finally {
            fixture.close()
        }
    }

    private fun fixture(
        io: CoroutineDispatcher,
        verification: Int,
        failFirstAbandon: Boolean = false,
    ): Fixture {
        val context = RuntimeEnvironment.getApplication() as Context
        val providerAuthority = "${context.packageName}.docprov"
        val provider = RecordingDocumentsProvider()
        provider.attachInfo(
            context,
            ProviderInfo().apply {
                authority = providerAuthority
                // DocumentsProvider.attachInfo refuses outright unless both are
                // set: a documents provider that is not exported and does not
                // grant URI permissions could never be reached through the
                // system picker, which is the whole path under test here.
                exported = true
                grantUriPermissions = true
                readPermission = android.Manifest.permission.MANAGE_DOCUMENTS
                writePermission = android.Manifest.permission.MANAGE_DOCUMENTS
            },
        )
        ShadowContentResolver.registerProviderInternal(providerAuthority, provider)
        val suffix = System.nanoTime().toString()
        val destination = provider.getFile("backup-copy-$suffix.taffy-backup").apply {
            delete()
            createNewFile()
        }
        val archive = File(context.cacheDir, "backup-stage-$suffix").apply {
            writeBytes("authenticated encrypted archive".toByteArray())
        }
        val readback = File(context.cacheDir, "backup-readback-$suffix").apply { delete() }
        val native = ExportNativeWindow(archive, readback, verification, failFirstAbandon)
        val host = ChromiumBackupWindowHost(context.contentResolver, io, native)
        assertTrue(host.activate())
        return Fixture(
            host,
            native,
            provider,
            destination,
            archive,
            readback,
            DocumentsContract.buildDocumentUri(
                providerAuthority,
                provider.getDocumentId(destination),
            ),
        )
    }

    private class Fixture(
        val host: ChromiumBackupWindowHost,
        val native: ExportNativeWindow,
        val provider: RecordingDocumentsProvider,
        val destination: File,
        private val archive: File,
        private val readback: File,
        val destinationUri: Uri,
    ) {
        fun export(): com.taffygo.browser.ui.app.BackupExportResult {
            val session = requireNotNull(
                host.openRecoveryKeySession(BackupRecoveryKeySession.Mode.CREATE),
            )
            assertNotNull(session.takeGeneratedKeyForDisplay())
            assertEquals(
                BackupRecoveryKeySession.Acceptance.ACCEPTED,
                session.confirmKeyRetained(),
            )
            var preparation: BackupWindowHost.ExportPreparation? = null
            host.prepareExport(session, setOf(BackupWindowHost.ContentClass.LIBRARY)) {
                preparation = it
            }
            assertEquals(
                BackupWindowHost.ExportPreparation.Ready(archive.length()),
                preparation,
            )
            return runBlocking { host.writePreparedExport(session, destinationUri) }
        }

        fun close() {
            host.close()
            destination.delete()
            archive.delete()
            readback.delete()
        }
    }

    private class RecordingDocumentsProvider : TestDocumentsProvider() {
        @Volatile var queryCalls = 0
        @Volatile var deleteCalls = 0
        @Volatile var deleteThread: String? = null

        override fun queryDocument(documentId: String, projection: Array<out String>?): Cursor {
            queryCalls++
            return super.queryDocument(documentId, projection)
        }

        override fun deleteDocument(documentId: String) {
            deleteCalls++
            deleteThread = Thread.currentThread().name
            super.deleteDocument(documentId)
        }
    }

    private class HoldingDispatcher : CoroutineDispatcher() {
        private var holding = false
        private var pending: Runnable? = null

        override fun dispatch(context: CoroutineContext, block: Runnable) {
            if (holding) {
                check(pending == null)
                pending = block
            } else {
                block.run()
            }
        }

        fun hold() {
            check(pending == null)
            holding = true
        }

        fun runHeld() {
            holding = false
            requireNotNull(pending.also { pending = null }).run()
        }
    }

    private class ExportNativeWindow(
        private val archive: File,
        private val readback: File,
        private val verification: Int,
        private var failNextAbandon: Boolean,
    ) : BackupNativeWindow {
        var abandonAttempts = 0
        private var active = false
        private var closed = false

        override fun activate(): Boolean {
            if (closed) return false
            active = true
            return true
        }
        override fun deactivate() { active = false }
        override fun beginRecoveryKeySession(mode: Int) = if (active) "copy-operation" else null
        override fun takeGeneratedKeyForDisplay(operationId: String) = charArrayOf('K')
        override fun confirmKeyRetained(operationId: String) = TaffyBackupWorkflowBridge.ACCEPTED
        override fun acceptEnteredKey(operationId: String, key: CharArray) = TaffyBackupWorkflowBridge.UNAVAILABLE
        override fun prepareExport(
            operationId: String,
            selection: IntArray,
            callback: (NativeExportPreparation) -> Unit,
        ): Boolean {
            if (!active || closed) return false
            callback(NativeExportPreparation(archive.length(), TaffyBackupWorkflowBridge.PREPARED))
            return true
        }
        override fun maximumImportBytes(operationId: String) = -1L
        override fun openEncryptedArchiveReadFd(operationId: String) =
            handOutFd(ParcelFileDescriptor.open(archive, ParcelFileDescriptor.MODE_READ_ONLY))
        override fun openReadbackWriteFd(operationId: String, expectedBytes: Long) =
            handOutFd(
                ParcelFileDescriptor.open(
                    readback,
                    ParcelFileDescriptor.MODE_CREATE or ParcelFileDescriptor.MODE_TRUNCATE or
                        ParcelFileDescriptor.MODE_WRITE_ONLY,
                ),
            )

        // Robolectric shadows ParcelFileDescriptor.getFd() but not detachFd(),
        // which reaches libcore's FileDescriptor.getOwnerId$() and does not
        // exist on a JVM: every test that reached this seam died with
        // NoSuchMethodError rather than exercising the transfer. Hand out the
        // raw descriptor and keep the wrapper instead. The code under test owns
        // and closes the descriptor exactly as it would after a detach; the
        // retained wrapper is never closed again here, so there is no second
        // close. What this cannot stand in for is the ownership transfer
        // itself, which is a device claim and belongs to taffy_public_test_apk.
        private val handedOut = mutableListOf<ParcelFileDescriptor>()

        private fun handOutFd(descriptor: ParcelFileDescriptor): Int {
            handedOut += descriptor
            return descriptor.fd
        }
        override fun verifyEncryptedReadback(operationId: String) = verification
        override fun openEncryptedImportWriteFd(operationId: String, maximumBytes: Long) = -1
        override fun inspectImportedArchive(operationId: String, actualBytes: Long) =
            TaffyBackupWorkflowBridge.UNAVAILABLE
        override fun prepareImportedRestore(
            operationId: String,
            targetProfileLabel: String,
            callback: (NativeRestorePreparation) -> Unit,
        ) = false
        override fun confirmAndStageRestore(reviewToken: Long, callback: (Int) -> Unit) = false
        override fun commitRestore(reviewToken: Long, callback: (Int) -> Unit) = false
        override fun resolveRestore(reviewToken: Long, choice: Int, callback: (Int) -> Unit) = false
        override fun discoverInterruptedRestore(
            callback: (NativeRestoreDiscovery) -> Unit,
        ): NativeRestoreDiscoveryRequest? = null
        override fun resolveRecoveredRestore(
            recoveredReviewToken: Long,
            choice: Int,
            callback: (Int) -> Unit,
        ) = false
        override fun abandonRecoveredRestoreReview(recoveredReviewToken: Long) = Unit
        override fun abandon(operationId: String) {
            abandonAttempts++
            if (failNextAbandon) {
                failNextAbandon = false
                throw IllegalStateException("injected withdrawal failure")
            }
        }
        override fun close() { closed = true }
    }
}

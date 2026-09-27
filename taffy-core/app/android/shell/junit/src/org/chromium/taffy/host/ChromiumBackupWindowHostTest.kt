// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.host

import android.net.Uri
import com.taffygo.browser.ui.app.BackupDeletionRequest
import com.taffygo.browser.ui.app.BackupRecoveryKeySession
import com.taffygo.browser.ui.app.BackupWindowHost
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.runBlocking
import org.chromium.base.test.BaseRobolectricTestRunner
import org.chromium.taffy.browser.TaffyBackupWorkflowBridge
import org.chromium.taffy.core_service.mojom.BackupRecordKind
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RuntimeEnvironment

@RunWith(BaseRobolectricTestRunner::class)
class ChromiumBackupWindowHostTest {
    @Test
    fun `new key authority follows only the exact active window`() {
        val native = FakeNativeWindow()
        val host = host(native)

        assertNull(host.openRecoveryKeySession(BackupRecoveryKeySession.Mode.CREATE))
        assertTrue(host.activate())
        val session = requireNotNull(
            host.openRecoveryKeySession(BackupRecoveryKeySession.Mode.CREATE),
        )
        host.deactivate()

        assertNull(session.takeGeneratedKeyForDisplay())
        assertNull(host.openRecoveryKeySession(BackupRecoveryKeySession.Mode.RESTORE))
        assertTrue(host.activate())
        assertNotNull(session.takeGeneratedKeyForDisplay())
        session.cancel()
        assertEquals(listOf("operation-1"), native.abandoned)
    }

    @Test
    fun `deletion selection and recovery key sessions exclude each other`() {
        val host = host(FakeNativeWindow())
        assertNull(host.openBackupDeletionRequest())
        assertTrue(host.activate())

        val deletion = requireNotNull(host.openBackupDeletionRequest())
        assertNull(host.openBackupDeletionRequest())
        assertNull(host.openRecoveryKeySession(BackupRecoveryKeySession.Mode.CREATE))
        deletion.close()

        val session = requireNotNull(
            host.openRecoveryKeySession(BackupRecoveryKeySession.Mode.RESTORE),
        )
        assertNull(host.openBackupDeletionRequest())
        session.cancel()
        assertNotNull(host.openBackupDeletionRequest())
        host.close()
    }

    @Test
    fun `deletion request is exact one shot and window close withdraws it`() {
        val host = host(FakeNativeWindow())
        assertTrue(host.activate())
        val request = requireNotNull(host.openBackupDeletionRequest())
        val foreign = object : BackupDeletionRequest {
            override fun close() = Unit
        }

        assertNull(
            runBlocking {
                host.completeBackupDeletionSelection(
                    foreign,
                    Uri.parse("content://backup.test/foreign"),
                )
            },
        )
        assertNull(host.openBackupDeletionRequest())
        assertNull(
            runBlocking {
                host.completeBackupDeletionSelection(request, Uri.parse("file:///not-granted.aib"))
            },
        )
        assertNull(
            runBlocking {
                host.completeBackupDeletionSelection(
                    request,
                    Uri.parse("content://backup.test/replayed"),
                )
            },
        )

        val pending = requireNotNull(host.openBackupDeletionRequest())
        host.close()
        assertNull(
            runBlocking {
                host.completeBackupDeletionSelection(
                    pending,
                    Uri.parse("content://backup.test/after-close"),
                )
            },
        )
    }

    @Test
    fun `reentrant selection during native key open withdraws the unretained key operation`() {
        val native = FakeNativeWindow()
        val host = host(native)
        assertTrue(host.activate())
        var request: BackupDeletionRequest? = null
        native.duringKeyOpen = { request = host.openBackupDeletionRequest() }

        assertNull(host.openRecoveryKeySession(BackupRecoveryKeySession.Mode.CREATE))
        assertNotNull(request)
        assertEquals(listOf("operation-1"), native.abandoned)
        assertNull(host.openBackupDeletionRequest())
        assertNull(host.openRecoveryKeySession(BackupRecoveryKeySession.Mode.RESTORE))

        requireNotNull(request).close()
        native.duringKeyOpen = {}
        assertNotNull(host.openRecoveryKeySession(BackupRecoveryKeySession.Mode.RESTORE))
        host.close()
    }

    @Test
    fun `confirmed create key is consumed by the canonical six-kind selection`() {
        val native = FakeNativeWindow()
        val host = host(native)
        assertTrue(host.activate())
        val session = confirmedCreate(host)
        var result: BackupWindowHost.ExportPreparation? = null

        host.prepareExport(
            session,
            BackupWindowHost.ContentClass.entries.toSet(),
        ) { result = it }

        assertArrayEquals(
            intArrayOf(
                BackupRecordKind.ASSISTANT_CONFIGURATION,
                BackupRecordKind.SAVED_WORKSPACE,
                BackupRecordKind.LIBRARY_ENTRY,
                BackupRecordKind.MEMORY_RECORD,
                BackupRecordKind.USER_AUTHORED_SKILL,
                BackupRecordKind.LEARNED_PROCEDURE,
            ).sortedArray(),
            native.lastSelection,
        )
        assertNull(result)
        native.completePreparation(741L, TaffyBackupWorkflowBridge.PREPARED)
        assertEquals(BackupWindowHost.ExportPreparation.Ready(741L), result)
    }

    @Test
    fun `empty selection is refused without consuming the confirmed session`() {
        val native = FakeNativeWindow()
        val host = host(native)
        assertTrue(host.activate())
        val session = confirmedCreate(host)
        val results = mutableListOf<BackupWindowHost.ExportPreparation>()

        host.prepareExport(session, emptySet(), results::add)

        assertEquals(listOf(BackupWindowHost.ExportPreparation.Refused), results)
        assertEquals(0, native.prepareCalls)
        session.cancel()
        assertEquals(listOf("operation-1"), native.abandoned)
    }

    @Test
    fun `an admitted preparation may settle while paused but no new one begins`() {
        val native = FakeNativeWindow()
        val host = host(native)
        assertTrue(host.activate())
        val session = confirmedCreate(host)
        var result: BackupWindowHost.ExportPreparation? = null
        host.prepareExport(
            session,
            setOf(BackupWindowHost.ContentClass.LIBRARY),
        ) { result = it }

        host.deactivate()
        native.completePreparation(73L, TaffyBackupWorkflowBridge.PREPARED)

        assertEquals(BackupWindowHost.ExportPreparation.Ready(73L), result)
        assertNull(host.openRecoveryKeySession(BackupRecoveryKeySession.Mode.CREATE))
    }

    @Test
    fun `close withdraws sessions before settling an in-flight preparation`() {
        val native = FakeNativeWindow()
        val host = host(native)
        assertTrue(host.activate())
        val session = confirmedCreate(host)
        val results = mutableListOf<BackupWindowHost.ExportPreparation>()
        host.prepareExport(
            session,
            setOf(BackupWindowHost.ContentClass.MEMORY),
            results::add,
        )

        host.close()

        assertTrue(native.closed)
        assertEquals(listOf(BackupWindowHost.ExportPreparation.Unavailable), results)
        assertEquals(
            BackupRecoveryKeySession.Acceptance.UNAVAILABLE,
            session.confirmKeyRetained(),
        )
        host.close()
        assertEquals(1, native.closeCalls)
    }

    @Test
    fun `restore key admission obtains only the native-authorized archive bound`() {
        val native = FakeNativeWindow(maximumImportBytes = 9_001L)
        val host = host(native)
        assertTrue(host.activate())
        val session = requireNotNull(
            host.openRecoveryKeySession(BackupRecoveryKeySession.Mode.RESTORE),
        )

        assertNull(session.takeGeneratedKeyForDisplay())
        assertEquals(
            BackupRecoveryKeySession.Acceptance.ACCEPTED,
            session.acceptEnteredKey(charArrayOf('T', 'A', 'F', 'F', 'Y')),
        )
        assertEquals(1, native.maximumCalls)
        host.deactivate()
        session.cancel()
        assertEquals(listOf("operation-1"), native.abandoned)
    }

    @Test
    fun `export adapter failure cannot consume the session cancellation fallback`() {
        val native = FakeNativeWindow().apply { failNextAbandon = true }
        val host = host(native)
        assertTrue(host.activate())
        val session = confirmedCreate(host)
        host.prepareExport(
            session,
            setOf(BackupWindowHost.ContentClass.LIBRARY),
        ) {}
        native.completePreparation(73L, TaffyBackupWorkflowBridge.PREPARED)

        val result = runBlocking {
            host.writePreparedExport(session, Uri.parse("content://backup/export"))
        }
        assertEquals(com.taffygo.browser.ui.app.BackupDocumentTransfer.WriteResult.UNAVAILABLE, result.status)
        assertNull(result.copy)
        session.cancel()

        assertEquals(2, native.abandonAttempts)
        assertEquals(listOf("operation-1"), native.abandoned)
    }

    @Test
    fun `import adapter failure cannot consume the session cancellation fallback`() {
        val native = FakeNativeWindow().apply { failNextAbandon = true }
        val host = host(native)
        assertTrue(host.activate())
        val session = requireNotNull(
            host.openRecoveryKeySession(BackupRecoveryKeySession.Mode.RESTORE),
        )
        assertEquals(
            BackupRecoveryKeySession.Acceptance.ACCEPTED,
            session.acceptEnteredKey(charArrayOf('T', 'A', 'F', 'F', 'Y')),
        )

        org.junit.Assert.assertThrows(IllegalStateException::class.java) {
            runBlocking { host.abandon(session) }
        }
        session.cancel()

        assertEquals(2, native.abandonAttempts)
        assertEquals(listOf("operation-1"), native.abandoned)
    }

    @Test
    fun `cancellation stays nonthrowing and retryable until native withdrawal succeeds`() {
        val native = FakeNativeWindow().apply { failNextAbandon = true }
        val host = host(native)
        assertTrue(host.activate())
        val session = confirmedCreate(host)

        session.cancel()

        assertEquals(1, native.abandonAttempts)
        assertTrue(native.abandoned.isEmpty())
        assertNull(session.takeGeneratedKeyForDisplay())
        assertEquals(
            BackupRecoveryKeySession.Acceptance.UNAVAILABLE,
            session.confirmKeyRetained(),
        )
        session.cancel()
        session.cancel()

        assertEquals(2, native.abandonAttempts)
        assertEquals(listOf("operation-1"), native.abandoned)
    }

    private fun host(native: FakeNativeWindow) = ChromiumBackupWindowHost(
        RuntimeEnvironment.getApplication().contentResolver,
        Dispatchers.Unconfined,
        native,
    )

    private fun confirmedCreate(host: ChromiumBackupWindowHost): BackupRecoveryKeySession {
        val session = requireNotNull(
            host.openRecoveryKeySession(BackupRecoveryKeySession.Mode.CREATE),
        )
        assertArrayEquals(charArrayOf('K', 'E', 'Y'), session.takeGeneratedKeyForDisplay())
        assertEquals(
            BackupRecoveryKeySession.Acceptance.ACCEPTED,
            session.confirmKeyRetained(),
        )
        return session
    }
}

// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.host

import com.taffygo.browser.ui.app.BackupRestoreCommitResult
import com.taffygo.browser.ui.app.BackupRestoreResolutionChoice
import com.taffygo.browser.ui.app.BackupRestoreResolutionResult
import org.chromium.base.test.BaseRobolectricTestRunner
import org.chromium.taffy.browser.TaffyBackupRestoreWindow
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(BaseRobolectricTestRunner::class)
class ChromiumBackupRestoreResolutionHostTest {
    @Test
    fun `commit custody is never reissued after cancellation or callback loss`() {
        val fixture = stagedFixture()
        var result: BackupRestoreCommitResult? = null
        fixture.controller.commit(fixture.review) { result = it }
        fixture.native.failNextAbandon = true

        fixture.session.cancel()
        fixture.session.cancel()

        assertEquals(listOf(71L), fixture.native.committedTokens)
        assertEquals(1, fixture.native.abandonAttempts)
        fixture.native.completeCommit(71, TaffyBackupRestoreWindow.RESTORE_HIDDEN_CANDIDATE)
        assertEquals(BackupRestoreCommitResult.RECOVERY_REQUIRED, result)
        fixture.controller.commit(fixture.review) { result = it }
        assertEquals(BackupRestoreCommitResult.REFUSED, result)
        assertEquals(1, fixture.native.committedTokens.size)
    }

    @Test
    fun `every commit status has one conservative UI projection`() {
        val cases = listOf(
            TaffyBackupRestoreWindow.RESTORE_HIDDEN_CANDIDATE to
                BackupRestoreCommitResult.HIDDEN_CANDIDATE,
            TaffyBackupRestoreWindow.RESTORE_DEFINITELY_NOT_COMMITTED to
                BackupRestoreCommitResult.DEFINITELY_NOT_COMMITTED,
            TaffyBackupRestoreWindow.RESTORE_COMMIT_RECOVERY_REQUIRED to
                BackupRestoreCommitResult.RECOVERY_REQUIRED,
            TaffyBackupRestoreWindow.RESTORE_COMMIT_REFUSED to BackupRestoreCommitResult.REFUSED,
            TaffyBackupRestoreWindow.RESTORE_COMMIT_UNAVAILABLE to
                BackupRestoreCommitResult.UNAVAILABLE,
            99 to BackupRestoreCommitResult.RECOVERY_REQUIRED,
        )
        cases.forEach { (nativeStatus, expected) ->
            val fixture = stagedFixture()
            var result: BackupRestoreCommitResult? = null
            fixture.controller.commit(fixture.review) { result = it }
            fixture.native.completeCommit(71, nativeStatus)
            assertEquals(expected, result)
        }
    }

    @Test
    fun `definite noncommit retains the exact review for discard-only cleanup`() {
        val fixture = stagedFixture()
        var commit: BackupRestoreCommitResult? = null
        fixture.controller.commit(fixture.review) { commit = it }
        fixture.native.completeCommit(
            71,
            TaffyBackupRestoreWindow.RESTORE_DEFINITELY_NOT_COMMITTED,
        )

        assertEquals(BackupRestoreCommitResult.DEFINITELY_NOT_COMMITTED, commit)
        assertEquals(BackupSessionPhase.RESTORE_CANDIDATE, fixture.session.phase)
        assertTrue(fixture.native.abandoned.isEmpty())
        var invalidChoice: BackupRestoreResolutionResult? = null
        fixture.controller.resolve(
            fixture.review,
            BackupRestoreResolutionChoice.ACCEPT,
        ) { invalidChoice = it }
        assertEquals(BackupRestoreResolutionResult.REFUSED, invalidChoice)
        assertTrue(fixture.native.resolutions.isEmpty())

        var cleanup: BackupRestoreResolutionResult? = null
        fixture.controller.resolve(
            fixture.review,
            BackupRestoreResolutionChoice.DISCARD,
        ) { cleanup = it }
        assertEquals(
            listOf(71L to TaffyBackupRestoreWindow.RESTORE_DISCARD),
            fixture.native.resolutions,
        )
        fixture.native.completeResolution(
            71,
            TaffyBackupRestoreWindow.RESTORE_DEFINITELY_NOT_COMPLETED,
        )
        assertEquals(BackupRestoreResolutionResult.DEFINITELY_NOT_COMPLETED, cleanup)
        assertEquals(BackupSessionPhase.RESTORE_CANDIDATE, fixture.session.phase)

        invalidChoice = null
        fixture.controller.resolve(
            fixture.review,
            BackupRestoreResolutionChoice.ACCEPT,
        ) { invalidChoice = it }
        assertEquals(BackupRestoreResolutionResult.REFUSED, invalidChoice)
        assertEquals(1, fixture.native.resolutions.size)
    }

    @Test
    fun `every candidate resolution status has one conservative UI projection`() {
        val cases = listOf(
            TaffyBackupRestoreWindow.RESTORE_PUBLISHED to BackupRestoreResolutionResult.PUBLISHED,
            TaffyBackupRestoreWindow.RESTORE_VERIFIED_DELETED to
                BackupRestoreResolutionResult.VERIFIED_DELETED,
            TaffyBackupRestoreWindow.RESTORE_DEFINITELY_NOT_COMPLETED to
                BackupRestoreResolutionResult.DEFINITELY_NOT_COMPLETED,
            TaffyBackupRestoreWindow.RESTORE_RESOLUTION_RECOVERY_REQUIRED to
                BackupRestoreResolutionResult.RECOVERY_REQUIRED,
            TaffyBackupRestoreWindow.RESTORE_RESOLUTION_REFUSED to
                BackupRestoreResolutionResult.REFUSED,
            TaffyBackupRestoreWindow.RESTORE_RESOLUTION_UNAVAILABLE to
                BackupRestoreResolutionResult.UNAVAILABLE,
            99 to BackupRestoreResolutionResult.RECOVERY_REQUIRED,
        )
        cases.forEach { (nativeStatus, expected) ->
            val fixture = candidateFixture()
            var result: BackupRestoreResolutionResult? = null
            fixture.controller.resolve(fixture.review, BackupRestoreResolutionChoice.DISCARD) {
                result = it
            }
            fixture.native.completeResolution(71, nativeStatus)
            assertEquals(expected, result)
        }
    }

    @Test
    fun `known unfinished resolution retains the exact candidate for a later explicit choice`() {
        val retryable = listOf(
            TaffyBackupRestoreWindow.RESTORE_DEFINITELY_NOT_COMPLETED,
            TaffyBackupRestoreWindow.RESTORE_RESOLUTION_REFUSED,
            TaffyBackupRestoreWindow.RESTORE_RESOLUTION_UNAVAILABLE,
        )
        retryable.forEach { nativeStatus ->
            val fixture = candidateFixture()
            fixture.controller.resolve(
                fixture.review,
                BackupRestoreResolutionChoice.DISCARD,
            ) {}
            fixture.native.completeResolution(71, nativeStatus)

            assertEquals(BackupSessionPhase.RESTORE_CANDIDATE, fixture.session.phase)
            assertTrue(fixture.native.abandoned.isEmpty())
            fixture.controller.resolve(
                fixture.review,
                BackupRestoreResolutionChoice.ACCEPT,
            ) {}
            assertEquals(
                listOf(
                    71L to TaffyBackupRestoreWindow.RESTORE_DISCARD,
                    71L to TaffyBackupRestoreWindow.RESTORE_ACCEPT,
                ),
                fixture.native.resolutions,
            )
        }
    }

    @Test
    fun `unadmitted resolution leaves the exact hidden candidate retryable`() {
        val fixture = candidateFixture()
        fixture.native.admitRestoreRequests = false
        var result: BackupRestoreResolutionResult? = null

        fixture.controller.resolve(
            fixture.review,
            BackupRestoreResolutionChoice.DISCARD,
        ) { result = it }

        assertEquals(BackupRestoreResolutionResult.UNAVAILABLE, result)
        assertEquals(BackupSessionPhase.RESTORE_CANDIDATE, fixture.session.phase)
        assertTrue(fixture.native.resolutions.isEmpty())
        assertTrue(fixture.native.abandoned.isEmpty())
        fixture.native.admitRestoreRequests = true
        fixture.controller.resolve(
            fixture.review,
            BackupRestoreResolutionChoice.DISCARD,
        ) {}
        assertEquals(
            listOf(71L to TaffyBackupRestoreWindow.RESTORE_DISCARD),
            fixture.native.resolutions,
        )
    }
}

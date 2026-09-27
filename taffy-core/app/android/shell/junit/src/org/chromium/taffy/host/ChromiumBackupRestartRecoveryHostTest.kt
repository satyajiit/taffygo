// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.host

import com.taffygo.browser.ui.app.BackupRecoveryKeySession
import com.taffygo.browser.ui.app.BackupRestoreCommitResult
import com.taffygo.browser.ui.app.BackupRestoreDiscoveryResult
import com.taffygo.browser.ui.app.BackupRestoreResolutionChoice
import com.taffygo.browser.ui.app.BackupRestoreResolutionResult
import com.taffygo.browser.ui.app.BackupRestoreStageResult
import kotlinx.coroutines.Dispatchers
import org.chromium.base.test.BaseRobolectricTestRunner
import org.chromium.taffy.browser.TaffyBackupRestoreWindow
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RuntimeEnvironment

@RunWith(BaseRobolectricTestRunner::class)
class ChromiumBackupRestartRecoveryHostTest {
    @Test
    fun `inline Ready is adopted before return and never enters the live import lane`() {
        val fixture = fixture()
        fixture.native.duringDiscovery = { callback -> callback(readyDiscovery(91)) }
        var result: BackupRestoreDiscoveryResult? = null

        val request = fixture.host.discoverInterruptedRestore { result = it }

        assertNotNull(request)
        val ready = result as BackupRestoreDiscoveryResult.Ready
        request?.close()
        assertTrue(fixture.native.abandonedRecoveredReviews.isEmpty())
        assertNull(fixture.host.openRecoveryKeySession(BackupRecoveryKeySession.Mode.RESTORE))

        var stage: BackupRestoreStageResult? = null
        fixture.host.confirmAndStageImportedRestore(ready.review) { stage = it }
        var commit: BackupRestoreCommitResult? = null
        fixture.host.commitStagedImportedRestore(ready.review) { commit = it }
        assertEquals(BackupRestoreStageResult.REFUSED, stage)
        assertEquals(BackupRestoreCommitResult.REFUSED, commit)
        assertTrue(fixture.native.stagedTokens.isEmpty())
        assertTrue(fixture.native.committedTokens.isEmpty())

        var resolution: BackupRestoreResolutionResult? = null
        fixture.host.resolveImportedRestore(
            ready.review,
            BackupRestoreResolutionChoice.ACCEPT,
        ) { resolution = it }
        assertEquals(
            listOf(91L to TaffyBackupRestoreWindow.RESTORE_ACCEPT),
            fixture.native.recoveredResolutions,
        )
        assertTrue(fixture.native.resolutions.isEmpty())
        fixture.native.completeRecoveredResolution(
            91,
            TaffyBackupRestoreWindow.RESTORE_PUBLISHED,
        )
        assertEquals(BackupRestoreResolutionResult.PUBLISHED, resolution)
    }

    @Test
    fun `closing pending discovery suppresses a late Ready and abandons its exact token`() {
        val fixture = fixture()
        var result: BackupRestoreDiscoveryResult? = null
        val request = requireNotNull(fixture.host.discoverInterruptedRestore { result = it })
        val nativeRequest = fixture.native.discoveryRequests.single()

        request.close()
        nativeRequest.complete(readyDiscovery(92))

        assertNull(result)
        assertEquals(1, nativeRequest.closeCalls)
        assertEquals(listOf(92L), fixture.native.abandonedRecoveredReviews)
    }

    @Test
    fun `every nonaction discovery status preserves its detailed public meaning`() {
        val cases = listOf(
            TaffyBackupRestoreWindow.DISCOVERY_NONE to BackupRestoreDiscoveryResult.None,
            TaffyBackupRestoreWindow.DISCOVERY_SOURCE_UNAVAILABLE to
                BackupRestoreDiscoveryResult.SourceUnavailable,
            TaffyBackupRestoreWindow.DISCOVERY_PRECOMMIT to recovery(
                BackupRestoreDiscoveryResult.Reason.PRECOMMIT,
            ),
            TaffyBackupRestoreWindow.DISCOVERY_PRESENTATION_UNAVAILABLE to recovery(
                BackupRestoreDiscoveryResult.Reason.PRESENTATION_UNAVAILABLE,
            ),
            TaffyBackupRestoreWindow.DISCOVERY_SCHEMA_MISMATCH to recovery(
                BackupRestoreDiscoveryResult.Reason.SCHEMA_MISMATCH,
            ),
            TaffyBackupRestoreWindow.DISCOVERY_OUTCOME_UNKNOWN to recovery(
                BackupRestoreDiscoveryResult.Reason.OUTCOME_UNKNOWN,
            ),
            TaffyBackupRestoreWindow.DISCOVERY_CUSTODY_AMBIGUOUS to recovery(
                BackupRestoreDiscoveryResult.Reason.CUSTODY_AMBIGUOUS,
            ),
            TaffyBackupRestoreWindow.DISCOVERY_PUBLISHED to
                BackupRestoreDiscoveryResult.AlreadyKept,
            TaffyBackupRestoreWindow.DISCOVERY_VERIFIED_DELETED to
                BackupRestoreDiscoveryResult.AlreadyDiscarded,
            TaffyBackupRestoreWindow.DISCOVERY_UNAVAILABLE to
                BackupRestoreDiscoveryResult.Unavailable,
        )
        cases.forEach { (status, expected) ->
            val fixture = fixture()
            var result: BackupRestoreDiscoveryResult? = null
            fixture.host.discoverInterruptedRestore { result = it }

            fixture.native.discoveryRequests.single().complete(observation(status))

            assertEquals(expected, result)
            assertTrue(fixture.native.abandonedRecoveredReviews.isEmpty())
        }
    }

    @Test
    fun `malformed actionable discovery never exposes or leaks native review authority`() {
        val fixture = fixture()
        var result: BackupRestoreDiscoveryResult? = null
        fixture.host.discoverInterruptedRestore { result = it }

        fixture.native.discoveryRequests.single().complete(
            readyDiscovery(93).copy(cleanupOnly = true),
        )

        assertEquals(BackupRestoreDiscoveryResult.Unavailable, result)
        assertEquals(listOf(93L), fixture.native.abandonedRecoveredReviews)
    }

    @Test
    fun `cleanup review permits only discard and retains known unfinished outcomes`() {
        val fixture = fixture()
        var discovery: BackupRestoreDiscoveryResult? = null
        fixture.host.discoverInterruptedRestore { discovery = it }
        fixture.native.discoveryRequests.single().complete(readyDiscovery(94, cleanupOnly = true))
        val ready = discovery as BackupRestoreDiscoveryResult.Ready
        assertTrue(ready.cleanupOnly)

        var result: BackupRestoreResolutionResult? = null
        fixture.host.resolveImportedRestore(
            ready.review,
            BackupRestoreResolutionChoice.ACCEPT,
        ) { result = it }
        assertEquals(BackupRestoreResolutionResult.REFUSED, result)
        assertTrue(fixture.native.recoveredResolutions.isEmpty())

        fixture.host.resolveImportedRestore(
            ready.review,
            BackupRestoreResolutionChoice.DISCARD,
        ) { result = it }
        fixture.native.completeRecoveredResolution(
            94,
            TaffyBackupRestoreWindow.RESTORE_DEFINITELY_NOT_COMPLETED,
        )
        assertEquals(BackupRestoreResolutionResult.DEFINITELY_NOT_COMPLETED, result)
        fixture.host.resolveImportedRestore(
            ready.review,
            BackupRestoreResolutionChoice.DISCARD,
        ) {}
        assertEquals(
            List(2) { 94L to TaffyBackupRestoreWindow.RESTORE_DISCARD },
            fixture.native.recoveredResolutions,
        )
    }

    @Test
    fun `pending discovery settles while paused and is not silently withdrawn`() {
        val fixture = fixture()
        var result: BackupRestoreDiscoveryResult? = null
        fixture.host.discoverInterruptedRestore { result = it }
        val request = fixture.native.discoveryRequests.single()

        fixture.host.deactivate()
        assertEquals(0, request.closeCalls)
        request.complete(observation(TaffyBackupRestoreWindow.DISCOVERY_UNAVAILABLE))

        assertEquals(BackupRestoreDiscoveryResult.Unavailable, result)
        assertTrue(fixture.native.abandonedRecoveredReviews.isEmpty())
    }

    @Test
    fun `delivered review remains exact across pause and resume`() {
        val fixture = fixture()
        var discovery: BackupRestoreDiscoveryResult? = null
        fixture.host.discoverInterruptedRestore { discovery = it }
        fixture.native.discoveryRequests.single().complete(readyDiscovery(95))
        val review = (discovery as BackupRestoreDiscoveryResult.Ready).review

        fixture.host.deactivate()
        var paused: BackupRestoreResolutionResult? = null
        fixture.host.resolveImportedRestore(
            review,
            BackupRestoreResolutionChoice.ACCEPT,
        ) { paused = it }
        assertEquals(BackupRestoreResolutionResult.UNAVAILABLE, paused)
        assertTrue(fixture.native.recoveredResolutions.isEmpty())

        assertTrue(fixture.host.activate())
        fixture.host.resolveImportedRestore(
            review,
            BackupRestoreResolutionChoice.ACCEPT,
        ) {}
        assertEquals(
            listOf(95L to TaffyBackupRestoreWindow.RESTORE_ACCEPT),
            fixture.native.recoveredResolutions,
        )
    }

    @Test
    fun `explicit abandon is nonthrowing and invalidates only the recovered review`() {
        val fixture = fixture()
        var discovery: BackupRestoreDiscoveryResult? = null
        fixture.host.discoverInterruptedRestore { discovery = it }
        fixture.native.discoveryRequests.single().complete(readyDiscovery(96))
        val review = (discovery as BackupRestoreDiscoveryResult.Ready).review
        fixture.native.failNextRecoveredAbandon = true

        fixture.host.abandonImportedRestoreReview(review)
        fixture.host.abandonImportedRestoreReview(review)
        var result: BackupRestoreResolutionResult? = null
        fixture.host.resolveImportedRestore(
            review,
            BackupRestoreResolutionChoice.DISCARD,
        ) { result = it }

        assertEquals(1, fixture.native.recoveredAbandonAttempts)
        assertTrue(fixture.native.abandonedRecoveredReviews.isEmpty())
        assertEquals(BackupRestoreResolutionResult.RECOVERY_REQUIRED, result)
        requireNotNull(
            fixture.host.openRecoveryKeySession(BackupRecoveryKeySession.Mode.RESTORE),
        ).cancel()
    }

    private fun fixture(): RestartFixture {
        val native = FakeNativeWindow()
        val host = ChromiumBackupWindowHost(
            RuntimeEnvironment.getApplication().contentResolver,
            Dispatchers.Unconfined,
            native,
        )
        check(host.activate())
        return RestartFixture(native, host)
    }

    private fun readyDiscovery(
        token: Long,
        cleanupOnly: Boolean = false,
    ) = NativeRestoreDiscovery(
        recoveredReviewToken = token,
        targetProfileLabel = "Restored profile",
        flattenedClassCounts = validCounts(),
        hasConflicts = false,
        canStage = true,
        cleanupOnly = cleanupOnly,
        status = if (cleanupOnly) TaffyBackupRestoreWindow.DISCOVERY_CLEANUP_REQUIRED
        else TaffyBackupRestoreWindow.DISCOVERY_ROLLBACK_AVAILABLE,
    )

    private fun observation(status: Int) = NativeRestoreDiscovery(
        recoveredReviewToken = 0,
        targetProfileLabel = "",
        flattenedClassCounts = intArrayOf(),
        hasConflicts = false,
        canStage = false,
        cleanupOnly = false,
        status = status,
    )

    private fun recovery(reason: BackupRestoreDiscoveryResult.Reason) =
        BackupRestoreDiscoveryResult.RecoveryRequired(reason)

    private data class RestartFixture(
        val native: FakeNativeWindow,
        val host: ChromiumBackupWindowHost,
    )
}

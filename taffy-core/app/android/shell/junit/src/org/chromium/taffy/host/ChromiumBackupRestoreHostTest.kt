// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.host

import com.taffygo.browser.ui.app.BackupRecoveryKeySession
import com.taffygo.browser.ui.app.BackupRestoreCommitResult
import com.taffygo.browser.ui.app.BackupRestorePreparation
import com.taffygo.browser.ui.app.BackupRestoreResolutionChoice
import com.taffygo.browser.ui.app.BackupRestoreResolutionResult
import com.taffygo.browser.ui.app.BackupRestoreReview
import com.taffygo.browser.ui.app.BackupRestoreStageResult
import org.chromium.base.test.BaseRobolectricTestRunner
import org.chromium.taffy.browser.TaffyBackupRestoreWindow
import org.chromium.taffy.core_service.mojom.BackupRecordKind
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotSame
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(BaseRobolectricTestRunner::class)
class ChromiumBackupRestoreHostTest {
    @Test
    fun `bounded six-class presentation drives the exact explicit workflow`() {
        val fixture = fixture()
        val counts = validCounts().also {
            it[1] = 2
            it[2] = 1 // A tombstone is valid in a new-profile plan.
        }

        val review = ready(fixture, counts)
        counts[1] = 99_999
        val first = review.summary
        val second = review.summary

        assertEquals(6, first.selectedClasses.size)
        assertEquals(2, first.selectedClasses.first().createCount)
        assertEquals(1, first.selectedClasses.first().deletionCount)
        assertTrue(first.canStage)
        assertNotSame(first.selectedClasses, second.selectedClasses)
        @Suppress("UNCHECKED_CAST")
        (first.selectedClasses as MutableList).clear()
        assertEquals(6, review.summary.selectedClasses.size)

        var stage: BackupRestoreStageResult? = null
        fixture.controller.stage(review) { stage = it }
        assertEquals(listOf(71L), fixture.native.stagedTokens)
        fixture.native.completeStage(71, TaffyBackupRestoreWindow.RESTORE_STAGED)
        assertEquals(BackupRestoreStageResult.STAGED, stage)

        var commit: BackupRestoreCommitResult? = null
        fixture.controller.commit(review) { commit = it }
        assertEquals(listOf(71L), fixture.native.committedTokens)
        fixture.native.completeCommit(71, TaffyBackupRestoreWindow.RESTORE_HIDDEN_CANDIDATE)
        assertEquals(BackupRestoreCommitResult.HIDDEN_CANDIDATE, commit)

        var resolution: BackupRestoreResolutionResult? = null
        fixture.controller.resolve(review, BackupRestoreResolutionChoice.ACCEPT) { resolution = it }
        assertEquals(
            listOf(71L to TaffyBackupRestoreWindow.RESTORE_ACCEPT),
            fixture.native.resolutions,
        )
        fixture.native.completeResolution(71, TaffyBackupRestoreWindow.RESTORE_PUBLISHED)
        assertEquals(BackupRestoreResolutionResult.PUBLISHED, resolution)
        assertEquals(listOf("operation"), fixture.native.abandoned)

        fixture.controller.commit(review) { commit = it }
        assertEquals(BackupRestoreCommitResult.REFUSED, commit)
        assertEquals(1, fixture.native.committedTokens.size)
    }

    @Test
    fun `target labels are exact bounded presentation and invalid input is not consumed`() {
        val fixture = fixture()
        val invalid = listOf(
            "",
            " leading",
            "trailing ",
            "line\nbreak",
            "x".repeat(41),
            "\uD83D\uDE00".repeat(21),
        )

        invalid.forEach { label ->
            var result: BackupRestorePreparation? = null
            fixture.controller.prepare(fixture.session, label) { result = it }
            assertEquals(BackupRestorePreparation.Refused, result)
        }

        assertEquals(0, fixture.native.restorePrepareCalls)
        assertEquals(BackupSessionPhase.IMPORT_VERIFIED, fixture.session.phase)
        val c1Label = "Profile\u0080name"
        var result: BackupRestorePreparation? = null
        fixture.controller.prepare(fixture.session, c1Label) { result = it }
        assertEquals(null, result)
        assertEquals(1, fixture.native.restorePrepareCalls)
        assertEquals(c1Label, fixture.native.lastTargetLabel)
        fixture.native.completeRestorePreparation(
            NativeRestorePreparation(
                71,
                c1Label,
                validCounts(),
                hasConflicts = false,
                canStage = true,
                status = TaffyBackupRestoreWindow.RESTORE_PREPARED,
            ),
        )
        assertTrue(result is BackupRestorePreparation.Ready)
    }

    @Test
    fun `malformed native summaries never mint review authority`() {
        val malformed = listOf(
            validCounts().also { it[7] = BackupRecordKind.ASSISTANT_CONFIGURATION },
            validCounts().also { it[0] = BackupRecordKind.BOOKMARK },
            validCounts().also { it[1] = -1 },
            validCounts().also { it[1] = 100_001 },
        )
        malformed.forEach { counts ->
            assertMalformed(counts = counts)
        }
        assertMalformed(hasConflicts = true)
        assertMalformed(label = "Different label")
        assertMalformed(reviewToken = 0)
    }

    @Test
    fun `unsupported plan actions force check-only presentation`() {
        val fixture = fixture()
        val counts = validCounts().also {
            it[3] = 1 // already-present
        }

        val review = ready(fixture, counts, canStage = true)

        assertFalse(review.summary.canStage)
        var result: BackupRestoreStageResult? = null
        fixture.controller.stage(review) { result = it }
        assertEquals(BackupRestoreStageResult.REFUSED, result)
        assertTrue(fixture.native.stagedTokens.isEmpty())
    }

    @Test
    fun `review identity phase and active window are all required`() {
        val first = fixture()
        val second = fixture()
        val review = ready(first)
        val impostor = object : BackupRestoreReview {
            override val summary = review.summary
        }
        val results = mutableListOf<BackupRestoreStageResult>()

        first.controller.stage(impostor, results::add)
        second.controller.stage(review, results::add)
        first.state.active = false
        first.controller.stage(review, results::add)

        assertEquals(List(3) { BackupRestoreStageResult.REFUSED }, results)
        assertTrue(first.native.stagedTokens.isEmpty())
        first.state.active = true
        first.controller.stage(review, results::add)
        assertEquals(listOf(71L), first.native.stagedTokens)
    }

    private fun assertMalformed(
        counts: IntArray = validCounts(),
        label: String = "Restored profile",
        reviewToken: Long = 71,
        hasConflicts: Boolean = false,
    ) {
        val fixture = fixture()
        var result: BackupRestorePreparation? = null
        fixture.controller.prepare(fixture.session, "Restored profile") { result = it }
        fixture.native.completeRestorePreparation(
            NativeRestorePreparation(
                reviewToken,
                label,
                counts,
                hasConflicts,
                canStage = true,
                status = TaffyBackupRestoreWindow.RESTORE_PREPARED,
            ),
        )
        assertEquals(BackupRestorePreparation.Unavailable, result)
        assertEquals(listOf("operation"), fixture.native.abandoned)
    }
}

internal data class RestoreFixture(
    val native: FakeNativeWindow,
    val state: ChromiumBackupWindowState,
    val session: ChromiumBackupSession,
    val controller: ChromiumBackupRestoreController,
)

internal data class ReviewFixture(
    val fixture: RestoreFixture,
    val review: BackupRestoreReview,
) {
    val native get() = fixture.native
    val session get() = fixture.session
    val controller get() = fixture.controller
}

internal fun fixture(): RestoreFixture {
    val native = FakeNativeWindow()
    check(native.activate())
    val state = ChromiumBackupWindowState(native).also { it.active = true }
    val session = ChromiumBackupSession(state, "operation", BackupRecoveryKeySession.Mode.RESTORE)
    session.phase = BackupSessionPhase.IMPORT_VERIFIED
    state.sessions += session
    return RestoreFixture(native, state, session, ChromiumBackupRestoreController(state))
}

internal fun ready(
    fixture: RestoreFixture,
    counts: IntArray = validCounts(),
    canStage: Boolean = true,
): BackupRestoreReview {
    var result: BackupRestorePreparation? = null
    fixture.controller.prepare(fixture.session, "Restored profile") { result = it }
    fixture.native.completeRestorePreparation(
        NativeRestorePreparation(
            71,
            "Restored profile",
            counts,
            hasConflicts = false,
            canStage = canStage,
            status = TaffyBackupRestoreWindow.RESTORE_PREPARED,
        ),
    )
    return (result as BackupRestorePreparation.Ready).review
}

internal fun stagedFixture(): ReviewFixture {
    val fixture = fixture()
    val review = ready(fixture)
    fixture.controller.stage(review) {}
    fixture.native.completeStage(71, TaffyBackupRestoreWindow.RESTORE_STAGED)
    return ReviewFixture(fixture, review)
}

internal fun candidateFixture(): ReviewFixture {
    val staged = stagedFixture()
    staged.controller.commit(staged.review) {}
    staged.native.completeCommit(71, TaffyBackupRestoreWindow.RESTORE_HIDDEN_CANDIDATE)
    return staged
}

internal fun validCounts(): IntArray = intArrayOf(
    BackupRecordKind.ASSISTANT_CONFIGURATION, 1, 0, 0, 0, 0, 0,
    BackupRecordKind.SAVED_WORKSPACE, 0, 0, 0, 0, 0, 0,
    BackupRecordKind.LIBRARY_ENTRY, 0, 0, 0, 0, 0, 0,
    BackupRecordKind.MEMORY_RECORD, 0, 0, 0, 0, 0, 0,
    BackupRecordKind.USER_AUTHORED_SKILL, 0, 0, 0, 0, 0, 0,
    BackupRecordKind.LEARNED_PROCEDURE, 0, 0, 0, 0, 0, 0,
)

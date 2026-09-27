// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.host

import com.taffygo.browser.ui.app.BackupRecoveryKeySession
import com.taffygo.browser.ui.app.BackupRestoreCommitResult
import com.taffygo.browser.ui.app.BackupRestoreDiscoveryRequest
import com.taffygo.browser.ui.app.BackupRestoreDiscoveryResult
import com.taffygo.browser.ui.app.BackupRestorePreparation
import com.taffygo.browser.ui.app.BackupRestoreResolutionChoice
import com.taffygo.browser.ui.app.BackupRestoreResolutionResult
import com.taffygo.browser.ui.app.BackupRestoreReview
import com.taffygo.browser.ui.app.BackupRestoreStageResult
import com.taffygo.browser.ui.app.BackupRestoreSummary
import org.chromium.taffy.browser.TaffyBackupRestoreWindow
import org.chromium.taffy.browser.TaffyBackupWorkflowBridge

/** Keeps native restore authority behind one window-bound opaque review capability. */
class ChromiumBackupRestoreController(
    private val state: ChromiumBackupWindowState,
) {
    private val restart = ChromiumBackupRestartRecoveryController(state)

    fun prepare(
        session: BackupRecoveryKeySession,
        targetProfileLabel: String,
        onResult: (BackupRestorePreparation) -> Unit,
    ) {
        val owned = session as? ChromiumBackupSession
        if (owned == null || !validTargetLabel(targetProfileLabel)) {
            onResult(BackupRestorePreparation.Refused)
            return
        }
        val valid = synchronized(state.lock) {
            if (state.closed || !state.active || owned !in state.sessions ||
                owned.phase != BackupSessionPhase.IMPORT_VERIFIED ||
                owned.restoreCapability != null
            ) false else {
                owned.phase = BackupSessionPhase.RESTORE_PREPARING
                true
            }
        }
        if (!valid) {
            onResult(BackupRestorePreparation.Refused)
            return
        }
        val resultClaim = ResultClaim()
        val admitted = try {
            state.native?.prepareImportedRestore(owned.operationId, targetProfileLabel) { native ->
                if (!resultClaim.claim()) return@prepareImportedRestore
                val (projected, withdraw) = projectPreparation(owned, targetProfileLabel, native)
                if (withdraw) owned.cancel()
                onResult(projected)
            } ?: false
        } catch (_: RuntimeException) {
            if (resultClaim.claim()) {
                finish(owned)
                onResult(BackupRestorePreparation.Unavailable)
            }
            return
        }
        if (!admitted && resultClaim.claim()) {
            finish(owned)
            onResult(BackupRestorePreparation.Unavailable)
        }
    }

    fun stage(
        review: BackupRestoreReview,
        onResult: (BackupRestoreStageResult) -> Unit,
    ) {
        val owned = synchronized(state.lock) {
            activeReviewLocked(review, BackupSessionPhase.RESTORE_REVIEW)?.takeIf {
                it.snapshot.canStage && !it.snapshot.hasConflicts
            }?.also { it.session.phase = BackupSessionPhase.RESTORE_STAGING }
        }
        if (owned == null) {
            onResult(BackupRestoreStageResult.REFUSED)
            return
        }
        val resultClaim = ResultClaim()
        val admitted = try {
            state.native?.confirmAndStageRestore(owned.reviewToken) { status ->
                if (!resultClaim.claim()) return@confirmAndStageRestore
                val result = projectStage(owned, status)
                if (result != BackupRestoreStageResult.STAGED) finish(owned.session)
                onResult(result)
            } ?: false
        } catch (_: RuntimeException) {
            if (resultClaim.claim()) {
                finish(owned.session)
                onResult(BackupRestoreStageResult.UNAVAILABLE)
            }
            return
        }
        if (!admitted && resultClaim.claim()) {
            finish(owned.session)
            onResult(BackupRestoreStageResult.UNAVAILABLE)
        }
    }

    fun commit(
        review: BackupRestoreReview,
        onResult: (BackupRestoreCommitResult) -> Unit,
    ) {
        val owned = synchronized(state.lock) {
            activeReviewLocked(review, BackupSessionPhase.RESTORE_STAGED)?.also {
                it.session.phase = BackupSessionPhase.RESTORE_COMMITTING
            }
        }
        if (owned == null) {
            onResult(BackupRestoreCommitResult.REFUSED)
            return
        }
        val resultClaim = ResultClaim()
        val admitted = try {
            state.native?.commitRestore(owned.reviewToken) { status ->
                if (!resultClaim.claim()) return@commitRestore
                val result = projectCommit(owned, status)
                if (result != BackupRestoreCommitResult.HIDDEN_CANDIDATE &&
                    result != BackupRestoreCommitResult.DEFINITELY_NOT_COMMITTED
                ) finish(owned.session)
                onResult(result)
            } ?: false
        } catch (_: RuntimeException) {
            if (resultClaim.claim()) {
                markPhysicalCustody(owned.session)
                finish(owned.session)
                onResult(BackupRestoreCommitResult.RECOVERY_REQUIRED)
            }
            return
        }
        if (admitted) {
            markPhysicalCustody(owned.session)
        } else if (resultClaim.claim()) {
            finish(owned.session)
            onResult(BackupRestoreCommitResult.UNAVAILABLE)
        }
    }

    fun resolve(
        review: BackupRestoreReview,
        choice: BackupRestoreResolutionChoice,
        onResult: (BackupRestoreResolutionResult) -> Unit,
    ) {
        if (restart.resolveIfRecovered(review, choice, onResult)) return
        val owned = synchronized(state.lock) {
            activeReviewLocked(review, BackupSessionPhase.RESTORE_CANDIDATE)?.takeIf {
                !it.cleanupOnly || choice == BackupRestoreResolutionChoice.DISCARD
            }?.also {
                it.session.phase = BackupSessionPhase.RESTORE_RESOLVING
            }
        }
        if (owned == null) {
            onResult(BackupRestoreResolutionResult.REFUSED)
            return
        }
        val resultClaim = ResultClaim()
        val admitted = try {
            state.native?.resolveRestore(owned.reviewToken, choice.toNative()) { status ->
                if (!resultClaim.claim()) return@resolveRestore
                val result = projectResolution(owned, status)
                if (!result.keepsHiddenCandidate()) finish(owned.session)
                onResult(result)
            } ?: false
        } catch (_: RuntimeException) {
            if (resultClaim.claim()) {
                finish(owned.session)
                onResult(BackupRestoreResolutionResult.RECOVERY_REQUIRED)
            }
            return
        }
        if (!admitted && resultClaim.claim()) {
            restoreCandidate(owned)
            onResult(BackupRestoreResolutionResult.UNAVAILABLE)
        }
    }

    fun discoverInterruptedRestore(
        onResult: (BackupRestoreDiscoveryResult) -> Unit,
    ): BackupRestoreDiscoveryRequest? = restart.discover(onResult)

    fun abandonRecoveredReview(review: BackupRestoreReview) = restart.abandon(review)

    fun close() = restart.close()

    private fun projectPreparation(
        session: ChromiumBackupSession,
        requestedLabel: String,
        native: NativeRestorePreparation,
    ): Pair<BackupRestorePreparation, Boolean> = synchronized(state.lock) {
        if (session !in state.sessions || session.phase != BackupSessionPhase.RESTORE_PREPARING) {
            return@synchronized BackupRestorePreparation.Unavailable to true
        }
        if (native.status == TaffyBackupWorkflowBridge.REFUSED) {
            state.finishLocked(session)
            return@synchronized BackupRestorePreparation.Refused to true
        }
        val summary = if (native.status == TaffyBackupRestoreWindow.RESTORE_PREPARED) {
            decodeSummary(requestedLabel, native)
        } else null
        val tokenInUse = native.reviewToken <= 0 || state.sessions.any {
            (it.restoreCapability as? RestoreReview)?.reviewToken == native.reviewToken
        }
        if (summary == null || tokenInUse) {
            state.finishLocked(session)
            BackupRestorePreparation.Unavailable to true
        } else {
            val review = RestoreReview(session, native.reviewToken, summary)
            session.restoreCapability = review
            session.phase = BackupSessionPhase.RESTORE_REVIEW
            BackupRestorePreparation.Ready(review) to false
        }
    }

    private fun projectStage(review: RestoreReview, status: Int): BackupRestoreStageResult =
        synchronized(state.lock) {
            if (!isCurrentLocked(review, BackupSessionPhase.RESTORE_STAGING)) {
                return@synchronized BackupRestoreStageResult.UNAVAILABLE
            }
            when (status) {
                TaffyBackupRestoreWindow.RESTORE_STAGED -> {
                    review.session.phase = BackupSessionPhase.RESTORE_STAGED
                    BackupRestoreStageResult.STAGED
                }
                TaffyBackupWorkflowBridge.REFUSED -> BackupRestoreStageResult.REFUSED
                else -> BackupRestoreStageResult.UNAVAILABLE
            }
        }

    private fun projectCommit(review: RestoreReview, status: Int): BackupRestoreCommitResult =
        synchronized(state.lock) {
            markPhysicalCustodyLocked(review.session)
            if (!isCurrentLocked(review, BackupSessionPhase.RESTORE_COMMITTING)) {
                return@synchronized BackupRestoreCommitResult.RECOVERY_REQUIRED
            }
            when (status) {
                TaffyBackupRestoreWindow.RESTORE_HIDDEN_CANDIDATE -> {
                    review.session.phase = BackupSessionPhase.RESTORE_CANDIDATE
                    BackupRestoreCommitResult.HIDDEN_CANDIDATE
                }
                TaffyBackupRestoreWindow.RESTORE_DEFINITELY_NOT_COMMITTED -> {
                    review.cleanupOnly = true
                    review.session.phase = BackupSessionPhase.RESTORE_CANDIDATE
                    BackupRestoreCommitResult.DEFINITELY_NOT_COMMITTED
                }
                TaffyBackupRestoreWindow.RESTORE_COMMIT_RECOVERY_REQUIRED ->
                    BackupRestoreCommitResult.RECOVERY_REQUIRED
                TaffyBackupRestoreWindow.RESTORE_COMMIT_REFUSED -> BackupRestoreCommitResult.REFUSED
                TaffyBackupRestoreWindow.RESTORE_COMMIT_UNAVAILABLE ->
                    BackupRestoreCommitResult.UNAVAILABLE
                else -> BackupRestoreCommitResult.RECOVERY_REQUIRED
            }
        }

    private fun projectResolution(review: RestoreReview, status: Int): BackupRestoreResolutionResult =
        synchronized(state.lock) {
            if (!isCurrentLocked(review, BackupSessionPhase.RESTORE_RESOLVING)) {
                return@synchronized BackupRestoreResolutionResult.RECOVERY_REQUIRED
            }
            when (status) {
                TaffyBackupRestoreWindow.RESTORE_PUBLISHED -> BackupRestoreResolutionResult.PUBLISHED
                TaffyBackupRestoreWindow.RESTORE_VERIFIED_DELETED ->
                    BackupRestoreResolutionResult.VERIFIED_DELETED
                TaffyBackupRestoreWindow.RESTORE_DEFINITELY_NOT_COMPLETED -> {
                    review.session.phase = BackupSessionPhase.RESTORE_CANDIDATE
                    BackupRestoreResolutionResult.DEFINITELY_NOT_COMPLETED
                }
                TaffyBackupRestoreWindow.RESTORE_RESOLUTION_RECOVERY_REQUIRED ->
                    BackupRestoreResolutionResult.RECOVERY_REQUIRED
                TaffyBackupRestoreWindow.RESTORE_RESOLUTION_REFUSED -> {
                    review.session.phase = BackupSessionPhase.RESTORE_CANDIDATE
                    BackupRestoreResolutionResult.REFUSED
                }
                TaffyBackupRestoreWindow.RESTORE_RESOLUTION_UNAVAILABLE -> {
                    review.session.phase = BackupSessionPhase.RESTORE_CANDIDATE
                    BackupRestoreResolutionResult.UNAVAILABLE
                }
                else -> BackupRestoreResolutionResult.RECOVERY_REQUIRED
            }
        }

    private fun restoreCandidate(review: RestoreReview) = synchronized(state.lock) {
        if (isCurrentLocked(review, BackupSessionPhase.RESTORE_RESOLVING)) {
            review.session.phase = BackupSessionPhase.RESTORE_CANDIDATE
        }
    }

    private fun activeReviewLocked(
        candidate: BackupRestoreReview,
        phase: BackupSessionPhase,
    ): RestoreReview? {
        val review = candidate as? RestoreReview ?: return null
        return review.takeIf {
            !state.closed && state.active && isCurrentLocked(it, phase)
        }
    }

    private fun isCurrentLocked(review: RestoreReview, phase: BackupSessionPhase): Boolean =
        review.session in state.sessions && review.session.restoreCapability === review &&
            review.session.phase == phase

    private fun markPhysicalCustody(session: ChromiumBackupSession) = synchronized(state.lock) {
        markPhysicalCustodyLocked(session)
    }

    private fun markPhysicalCustodyLocked(session: ChromiumBackupSession) {
        session.physicalCustodyTransferred = true
    }

    private fun finish(session: ChromiumBackupSession) {
        synchronized(state.lock) {
            if (session in state.sessions) state.finishLocked(session)
        }
        session.cancel()
    }

    private inner class RestoreReview(
        val session: ChromiumBackupSession,
        val reviewToken: Long,
        val snapshot: BackupRestoreSummary,
    ) : BackupRestoreReview {
        var cleanupOnly = false
        override val summary: BackupRestoreSummary
            get() = snapshot.defensiveCopy()
    }
}

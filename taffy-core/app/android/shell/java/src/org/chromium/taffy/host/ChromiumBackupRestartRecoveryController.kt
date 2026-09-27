// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.host

import com.taffygo.browser.ui.app.BackupRestoreDiscoveryRequest
import com.taffygo.browser.ui.app.BackupRestoreDiscoveryResult
import com.taffygo.browser.ui.app.BackupRestoreResolutionChoice
import com.taffygo.browser.ui.app.BackupRestoreResolutionResult
import com.taffygo.browser.ui.app.BackupRestoreReview
import com.taffygo.browser.ui.app.BackupRestoreSummary
import org.chromium.taffy.browser.TaffyBackupRestoreWindow

/** Window-local restart presentation; it never reconstructs a key-backed import operation. */
internal class ChromiumBackupRestartRecoveryController(
    private val state: ChromiumBackupWindowState,
) {
    private val requests = mutableSetOf<DiscoveryRequest>()
    private val reviews = mutableSetOf<RecoveredReview>()
    private var closed = false

    fun discover(
        onResult: (BackupRestoreDiscoveryResult) -> Unit,
    ): BackupRestoreDiscoveryRequest? {
        val owner = state.native ?: return null
        val request = synchronized(state.lock) {
            if (closed || state.closed || !state.active ||
                state.sessions.isNotEmpty() || state.deletionRequests.isNotEmpty() ||
                state.recoveredRestoreClaims.isNotEmpty()
            ) return null
            DiscoveryRequest(onResult).also {
                requests += it
                state.recoveredRestoreClaims += it
            }
        }
        val nativeRequest = try {
            owner.discoverInterruptedRestore(request::complete)
        } catch (_: RuntimeException) {
            null
        }
        if (nativeRequest == null) {
            if (request.wasDelivered()) return request
            request.close()
            return null
        }
        request.attach(nativeRequest)
        return request
    }

    fun resolveIfRecovered(
        candidate: BackupRestoreReview,
        choice: BackupRestoreResolutionChoice,
        onResult: (BackupRestoreResolutionResult) -> Unit,
    ): Boolean {
        val review = candidate as? RecoveredReview ?: return false
        val localResult = synchronized(state.lock) {
            when {
                closed || state.closed || review !in reviews ||
                    review !in state.recoveredRestoreClaims ->
                    BackupRestoreResolutionResult.RECOVERY_REQUIRED
                !state.active -> BackupRestoreResolutionResult.UNAVAILABLE
                review.phase != RecoveredPhase.CANDIDATE ->
                    BackupRestoreResolutionResult.RECOVERY_REQUIRED
                review.cleanupOnly && choice != BackupRestoreResolutionChoice.DISCARD ->
                    BackupRestoreResolutionResult.REFUSED
                else -> {
                    review.phase = RecoveredPhase.RESOLVING
                    null
                }
            }
        }
        if (localResult != null) {
            onResult(localResult)
            return true
        }
        val resultClaim = ResultClaim()
        val admitted = try {
            state.native?.resolveRecoveredRestore(review.recoveredReviewToken, choice.toNative()) {
                status ->
                if (!resultClaim.claim()) return@resolveRecoveredRestore
                val result = projectResolution(review, status)
                if (!result.keepsHiddenCandidate()) {
                    safeAbandon(review.recoveredReviewToken)
                }
                onResult(result)
            } ?: false
        } catch (_: RuntimeException) {
            if (resultClaim.claim()) {
                revoke(review)
                safeAbandon(review.recoveredReviewToken)
                onResult(BackupRestoreResolutionResult.RECOVERY_REQUIRED)
            }
            return true
        }
        if (!admitted && resultClaim.claim()) {
            revoke(review)
            safeAbandon(review.recoveredReviewToken)
            onResult(BackupRestoreResolutionResult.RECOVERY_REQUIRED)
        }
        return true
    }

    fun abandon(candidate: BackupRestoreReview) {
        val review = candidate as? RecoveredReview ?: return
        val token = synchronized(state.lock) {
            if (review !in reviews || review !in state.recoveredRestoreClaims) return
            if (!removeLocked(review)) return
            review.recoveredReviewToken
        }
        safeAbandon(token)
    }

    private fun withdrawAll() {
        val (pending, tokens) = takeAll()
        pending.forEach(DiscoveryRequest::closeNative)
        tokens.forEach(::safeAbandon)
    }

    fun close() {
        synchronized(state.lock) { closed = true }
        withdrawAll()
    }

    private fun projectResolution(
        review: RecoveredReview,
        status: Int,
    ): BackupRestoreResolutionResult = synchronized(state.lock) {
        if (!currentLocked(review, RecoveredPhase.RESOLVING)) {
            removeLocked(review)
            return@synchronized BackupRestoreResolutionResult.RECOVERY_REQUIRED
        }
        when (status) {
            TaffyBackupRestoreWindow.RESTORE_PUBLISHED -> {
                removeLocked(review)
                BackupRestoreResolutionResult.PUBLISHED
            }
            TaffyBackupRestoreWindow.RESTORE_VERIFIED_DELETED -> {
                removeLocked(review)
                BackupRestoreResolutionResult.VERIFIED_DELETED
            }
            TaffyBackupRestoreWindow.RESTORE_DEFINITELY_NOT_COMPLETED -> {
                review.phase = RecoveredPhase.CANDIDATE
                BackupRestoreResolutionResult.DEFINITELY_NOT_COMPLETED
            }
            TaffyBackupRestoreWindow.RESTORE_RESOLUTION_RECOVERY_REQUIRED -> {
                removeLocked(review)
                BackupRestoreResolutionResult.RECOVERY_REQUIRED
            }
            TaffyBackupRestoreWindow.RESTORE_RESOLUTION_REFUSED -> {
                review.phase = RecoveredPhase.CANDIDATE
                BackupRestoreResolutionResult.REFUSED
            }
            TaffyBackupRestoreWindow.RESTORE_RESOLUTION_UNAVAILABLE -> {
                review.phase = RecoveredPhase.CANDIDATE
                BackupRestoreResolutionResult.UNAVAILABLE
            }
            else -> {
                removeLocked(review)
                BackupRestoreResolutionResult.RECOVERY_REQUIRED
            }
        }
    }

    private fun revoke(review: RecoveredReview) = synchronized(state.lock) {
        removeLocked(review)
        review.phase = RecoveredPhase.CLOSED
    }

    private fun removeLocked(review: RecoveredReview): Boolean {
        state.recoveredRestoreClaims.remove(review)
        review.phase = RecoveredPhase.CLOSED
        return reviews.remove(review)
    }

    private fun currentLocked(review: RecoveredReview, phase: RecoveredPhase): Boolean =
        !closed && !state.closed && review in reviews && review in state.recoveredRestoreClaims &&
            review.phase == phase

    private fun takeAll(): Pair<List<DiscoveryRequest>, List<Long>> = synchronized(state.lock) {
        val pending = requests.toList()
        pending.forEach {
            it.settled = true
            it.callback = null
            state.recoveredRestoreClaims.remove(it)
        }
        requests.clear()
        val tokens = reviews.map(RecoveredReview::recoveredReviewToken)
        reviews.forEach {
            it.phase = RecoveredPhase.CLOSED
            state.recoveredRestoreClaims.remove(it)
        }
        reviews.clear()
        pending to tokens
    }

    private fun safeAbandon(recoveredReviewToken: Long) {
        try {
            state.native?.abandonRecoveredRestoreReview(recoveredReviewToken)
        } catch (_: RuntimeException) {
            // Public review withdrawal is idempotent and nonthrowing. Native window teardown
            // remains the final owner if this best-effort presentation withdrawal fails.
        }
    }

    private inner class DiscoveryRequest(
        var callback: ((BackupRestoreDiscoveryResult) -> Unit)?,
    ) : BackupRestoreDiscoveryRequest {
        var nativeRequest: NativeRestoreDiscoveryRequest? = null
        var settled = false
        private var delivered = false

        fun attach(owner: NativeRestoreDiscoveryRequest) {
            val closeNow = synchronized(state.lock) {
                if (settled || closed || state.closed || this !in requests) true else {
                    nativeRequest = owner
                    false
                }
            }
            if (closeNow) safeClose(owner)
        }

        fun complete(native: NativeRestoreDiscovery) {
            var abandonToken = 0L
            val delivery = synchronized(state.lock) {
                if (settled || closed || state.closed || this !in requests ||
                    callback == null
                ) {
                    if (native.recoveredReviewToken > 0 && !ownsTokenLocked(native.recoveredReviewToken)) {
                        abandonToken = native.recoveredReviewToken
                    }
                    requests.remove(this)
                    state.recoveredRestoreClaims.remove(this)
                    settled = true
                    callback = null
                    null
                } else {
                    val projected = projectRestoreDiscovery(native)
                    val result = when (projected) {
                        is ProjectedRestoreDiscovery.Ready -> {
                            if (!state.active) {
                                abandonToken = projected.recoveredReviewToken
                                BackupRestoreDiscoveryResult.Unavailable
                            } else if (ownsTokenLocked(projected.recoveredReviewToken)) {
                                BackupRestoreDiscoveryResult.Unavailable
                            } else {
                                val review = RecoveredReview(
                                    projected.recoveredReviewToken,
                                    projected.summary,
                                    projected.cleanupOnly,
                                )
                                reviews += review
                                state.recoveredRestoreClaims += review
                                BackupRestoreDiscoveryResult.Ready(review, projected.cleanupOnly)
                            }
                        }
                        is ProjectedRestoreDiscovery.Observation -> projected.result
                        ProjectedRestoreDiscovery.Malformed -> {
                            if (native.recoveredReviewToken > 0 &&
                                !ownsTokenLocked(native.recoveredReviewToken)
                            ) abandonToken = native.recoveredReviewToken
                            BackupRestoreDiscoveryResult.Unavailable
                        }
                    }
                    requests.remove(this)
                    state.recoveredRestoreClaims.remove(this)
                    settled = true
                    delivered = true
                    callback.also { callback = null }?.let { it to result }
                }
            }
            if (abandonToken > 0) safeAbandon(abandonToken)
            delivery?.let { (callback, result) -> callback(result) }
        }

        override fun close() {
            val owner = synchronized(state.lock) {
                if (settled || this !in requests) return
                settled = true
                callback = null
                requests.remove(this)
                state.recoveredRestoreClaims.remove(this)
                nativeRequest.also { nativeRequest = null }
            }
            if (owner != null) safeClose(owner)
        }

        fun closeNative() {
            val owner = synchronized(state.lock) {
                nativeRequest.also { nativeRequest = null }
            }
            if (owner != null) safeClose(owner)
        }

        fun wasDelivered(): Boolean = synchronized(state.lock) { delivered }
    }

    private fun ownsTokenLocked(token: Long): Boolean =
        reviews.any { it.recoveredReviewToken == token }

    private fun safeClose(request: NativeRestoreDiscoveryRequest) {
        try {
            request.close()
        } catch (_: RuntimeException) {
            // Closing an observational request is a nonthrowing public seam.
        }
    }

    private inner class RecoveredReview(
        val recoveredReviewToken: Long,
        val snapshot: BackupRestoreSummary,
        val cleanupOnly: Boolean,
    ) : BackupRestoreReview {
        var phase = RecoveredPhase.CANDIDATE
        override val summary: BackupRestoreSummary
            get() = snapshot.defensiveCopy()
    }

    private enum class RecoveredPhase { CANDIDATE, RESOLVING, CLOSED }
}

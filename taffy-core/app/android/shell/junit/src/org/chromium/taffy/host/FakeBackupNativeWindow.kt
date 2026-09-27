// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.host

import org.chromium.taffy.browser.TaffyBackupRestoreWindow
import org.chromium.taffy.browser.TaffyBackupWorkflowBridge

internal class FakeNativeWindow(
    private val maximumImportBytes: Long = 4_096L,
) : BackupNativeWindow {
    val abandoned = mutableListOf<String>()
    val stagedTokens = mutableListOf<Long>()
    val committedTokens = mutableListOf<Long>()
    val resolutions = mutableListOf<Pair<Long, Int>>()
    val recoveredResolutions = mutableListOf<Pair<Long, Int>>()
    val abandonedRecoveredReviews = mutableListOf<Long>()
    val discoveryRequests = mutableListOf<FakeDiscoveryRequest>()
    var lastSelection = intArrayOf()
    var lastTargetLabel: String? = null
    var prepareCalls = 0
    var restorePrepareCalls = 0
    var maximumCalls = 0
    var closed = false
    var closeCalls = 0
    var failNextAbandon = false
    var failNextRecoveredAbandon = false
    var abandonAttempts = 0
    var recoveredAbandonAttempts = 0
    var admitRestoreRequests = true
    var admitDiscoveryRequests = true
    var duringDiscovery: (((NativeRestoreDiscovery) -> Unit) -> Unit)? = null
    var duringKeyOpen: () -> Unit = {}
    private var active = false
    private var operationSequence = 0
    private var keyTaken = false
    private val preparations = mutableMapOf<String, (NativeExportPreparation) -> Unit>()
    private val restorePreparations = mutableMapOf<String, (NativeRestorePreparation) -> Unit>()
    private val stages = mutableMapOf<Long, (Int) -> Unit>()
    private val commits = mutableMapOf<Long, (Int) -> Unit>()
    private val resolutionCallbacks = mutableMapOf<Long, (Int) -> Unit>()
    private val recoveredResolutionCallbacks = mutableMapOf<Long, (Int) -> Unit>()

    override fun activate(): Boolean {
        if (closed) return false
        active = true
        return true
    }

    override fun deactivate() {
        active = false
    }

    override fun beginRecoveryKeySession(mode: Int): String? =
        if (active && !closed) "operation-${++operationSequence}".also { duringKeyOpen() } else null

    override fun takeGeneratedKeyForDisplay(operationId: String): CharArray? =
        if (!active || keyTaken) null else charArrayOf('K', 'E', 'Y').also { keyTaken = true }

    override fun confirmKeyRetained(operationId: String) =
        if (active) TaffyBackupWorkflowBridge.ACCEPTED else TaffyBackupWorkflowBridge.UNAVAILABLE

    override fun acceptEnteredKey(operationId: String, key: CharArray) =
        if (active) TaffyBackupWorkflowBridge.ACCEPTED else TaffyBackupWorkflowBridge.UNAVAILABLE

    override fun prepareExport(
        operationId: String,
        selection: IntArray,
        callback: (NativeExportPreparation) -> Unit,
    ): Boolean {
        if (!active || closed) return false
        prepareCalls++
        lastSelection = selection.copyOf()
        preparations[operationId] = callback
        return true
    }

    override fun maximumImportBytes(operationId: String): Long {
        maximumCalls++
        return maximumImportBytes
    }

    override fun openEncryptedArchiveReadFd(operationId: String) = -1
    override fun openReadbackWriteFd(operationId: String, expectedBytes: Long) = -1
    override fun verifyEncryptedReadback(operationId: String) = TaffyBackupWorkflowBridge.UNAVAILABLE
    override fun openEncryptedImportWriteFd(operationId: String, maximumBytes: Long) = -1
    override fun inspectImportedArchive(operationId: String, actualBytes: Long) =
        TaffyBackupWorkflowBridge.UNAVAILABLE

    override fun prepareImportedRestore(
        operationId: String,
        targetProfileLabel: String,
        callback: (NativeRestorePreparation) -> Unit,
    ): Boolean {
        if (!active || closed || !admitRestoreRequests) return false
        restorePrepareCalls++
        lastTargetLabel = targetProfileLabel
        restorePreparations[operationId] = callback
        return true
    }

    override fun confirmAndStageRestore(reviewToken: Long, callback: (Int) -> Unit): Boolean {
        if (!active || closed || !admitRestoreRequests) return false
        stagedTokens += reviewToken
        stages[reviewToken] = callback
        return true
    }

    override fun commitRestore(reviewToken: Long, callback: (Int) -> Unit): Boolean {
        if (!active || closed || !admitRestoreRequests) return false
        committedTokens += reviewToken
        commits[reviewToken] = callback
        return true
    }

    override fun resolveRestore(reviewToken: Long, choice: Int, callback: (Int) -> Unit): Boolean {
        if (!active || closed || !admitRestoreRequests) return false
        resolutions += reviewToken to choice
        resolutionCallbacks[reviewToken] = callback
        return true
    }

    override fun discoverInterruptedRestore(
        callback: (NativeRestoreDiscovery) -> Unit,
    ): NativeRestoreDiscoveryRequest? {
        if (!active || closed || !admitDiscoveryRequests) return null
        val request = FakeDiscoveryRequest(callback)
        discoveryRequests += request
        duringDiscovery?.invoke(callback)
        return request
    }

    override fun resolveRecoveredRestore(
        recoveredReviewToken: Long,
        choice: Int,
        callback: (Int) -> Unit,
    ): Boolean {
        if (!active || closed || !admitRestoreRequests) return false
        recoveredResolutions += recoveredReviewToken to choice
        recoveredResolutionCallbacks[recoveredReviewToken] = callback
        return true
    }

    override fun abandonRecoveredRestoreReview(recoveredReviewToken: Long) {
        recoveredAbandonAttempts++
        if (failNextRecoveredAbandon) {
            failNextRecoveredAbandon = false
            throw IllegalStateException("injected recovered-review withdrawal failure")
        }
        abandonedRecoveredReviews += recoveredReviewToken
        recoveredResolutionCallbacks.remove(recoveredReviewToken)
    }

    override fun abandon(operationId: String) {
        abandonAttempts++
        if (failNextAbandon) {
            failNextAbandon = false
            throw IllegalStateException("injected abandon failure")
        }
        preparations.remove(operationId)
        restorePreparations.remove(operationId)
        abandoned += operationId
    }

    override fun close() {
        if (closed) return
        closed = true
        closeCalls++
        val exportCallbacks = preparations.values.toList()
        val restoreCallbacks = restorePreparations.values.toList()
        val stageCallbacks = stages.values.toList()
        val commitCallbacks = commits.values.toList()
        val resolutionResults = resolutionCallbacks.values.toList()
        val recoveredResolutionResults = recoveredResolutionCallbacks.values.toList()
        preparations.clear()
        restorePreparations.clear()
        stages.clear()
        commits.clear()
        resolutionCallbacks.clear()
        recoveredResolutionCallbacks.clear()
        exportCallbacks.forEach {
            it(NativeExportPreparation(0L, TaffyBackupWorkflowBridge.UNAVAILABLE))
        }
        restoreCallbacks.forEach {
            it(NativeRestorePreparation(0, "", intArrayOf(), false, false,
                TaffyBackupWorkflowBridge.UNAVAILABLE))
        }
        stageCallbacks.forEach { it(TaffyBackupWorkflowBridge.UNAVAILABLE) }
        commitCallbacks.forEach { it(TaffyBackupRestoreWindow.RESTORE_COMMIT_RECOVERY_REQUIRED) }
        resolutionResults.forEach {
            it(TaffyBackupRestoreWindow.RESTORE_RESOLUTION_RECOVERY_REQUIRED)
        }
        recoveredResolutionResults.forEach {
            it(TaffyBackupRestoreWindow.RESTORE_RESOLUTION_RECOVERY_REQUIRED)
        }
    }

    fun completePreparation(archiveBytes: Long, status: Int) {
        val callback = preparations.values.single()
        preparations.clear()
        callback(NativeExportPreparation(archiveBytes, status))
    }

    fun completeRestorePreparation(preparation: NativeRestorePreparation) {
        val callback = restorePreparations.values.single()
        restorePreparations.clear()
        callback(preparation)
    }

    fun completeStage(token: Long, status: Int) = requireNotNull(stages.remove(token))(status)

    fun completeCommit(token: Long, status: Int) = requireNotNull(commits.remove(token))(status)

    fun completeResolution(token: Long, status: Int) =
        requireNotNull(resolutionCallbacks.remove(token))(status)

    fun completeRecoveredResolution(token: Long, status: Int) =
        requireNotNull(recoveredResolutionCallbacks.remove(token))(status)

    inner class FakeDiscoveryRequest(
        private val callback: (NativeRestoreDiscovery) -> Unit,
    ) : NativeRestoreDiscoveryRequest {
        var closeCalls = 0
            private set

        override fun close() {
            closeCalls++
        }

        /** Deliberately permits a late delivery so the host's second custody guard is testable. */
        fun complete(result: NativeRestoreDiscovery) = callback(result)
    }
}

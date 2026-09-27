// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.host

import com.taffygo.browser.ui.app.BackupRestoreClassSummary
import com.taffygo.browser.ui.app.BackupRestoreDiscoveryResult
import com.taffygo.browser.ui.app.BackupRestoreResolutionChoice
import com.taffygo.browser.ui.app.BackupRestoreResolutionResult
import com.taffygo.browser.ui.app.BackupRestoreSummary
import com.taffygo.browser.ui.app.BackupWindowHost
import org.chromium.taffy.browser.TaffyBackupRestoreWindow
import org.chromium.taffy.core_service.mojom.BackupRecordKind

private const val RESTORE_CLASS_WIDTH = 7
private const val MAX_RESTORE_ACTION_COUNT = 100_000L

/** Decodes only the bounded, ordered presentation summary; no native authority crosses it. */
internal fun decodeSummary(
    requestedLabel: String,
    native: NativeRestorePreparation,
): BackupRestoreSummary? = if (native.targetProfileLabel != requestedLabel) {
    null
} else {
    decodeSummary(
        native.targetProfileLabel,
        native.flattenedClassCounts,
        native.hasConflicts,
        native.canStage,
    )
}

internal sealed interface ProjectedRestoreDiscovery {
    data class Ready(
        val recoveredReviewToken: Long,
        val summary: BackupRestoreSummary,
        val cleanupOnly: Boolean,
    ) : ProjectedRestoreDiscovery

    data class Observation(
        val result: BackupRestoreDiscoveryResult,
    ) : ProjectedRestoreDiscovery

    data object Malformed : ProjectedRestoreDiscovery
}

/** Projects every closed discovery status while keeping the native token internal. */
internal fun projectRestoreDiscovery(
    native: NativeRestoreDiscovery,
): ProjectedRestoreDiscovery {
    val actionable = when (native.status) {
        TaffyBackupRestoreWindow.DISCOVERY_ROLLBACK_AVAILABLE -> false
        TaffyBackupRestoreWindow.DISCOVERY_CLEANUP_REQUIRED -> true
        else -> null
    }
    if (actionable != null) {
        val summary = decodeSummary(
            native.targetProfileLabel,
            native.flattenedClassCounts,
            native.hasConflicts,
            native.canStage,
        )
        return if (native.recoveredReviewToken > 0 &&
            native.cleanupOnly == actionable && summary != null
        ) {
            ProjectedRestoreDiscovery.Ready(
                native.recoveredReviewToken,
                summary,
                native.cleanupOnly,
            )
        } else ProjectedRestoreDiscovery.Malformed
    }
    if (native.recoveredReviewToken != 0L || native.targetProfileLabel.isNotEmpty() ||
        native.flattenedClassCounts.isNotEmpty() || native.hasConflicts || native.canStage ||
        native.cleanupOnly
    ) return ProjectedRestoreDiscovery.Malformed
    val result = when (native.status) {
        TaffyBackupRestoreWindow.DISCOVERY_NONE -> BackupRestoreDiscoveryResult.None
        TaffyBackupRestoreWindow.DISCOVERY_SOURCE_UNAVAILABLE ->
            BackupRestoreDiscoveryResult.SourceUnavailable
        TaffyBackupRestoreWindow.DISCOVERY_PRECOMMIT -> BackupRestoreDiscoveryResult.RecoveryRequired(
            BackupRestoreDiscoveryResult.Reason.PRECOMMIT,
        )
        TaffyBackupRestoreWindow.DISCOVERY_PRESENTATION_UNAVAILABLE ->
            BackupRestoreDiscoveryResult.RecoveryRequired(
                BackupRestoreDiscoveryResult.Reason.PRESENTATION_UNAVAILABLE,
            )
        TaffyBackupRestoreWindow.DISCOVERY_SCHEMA_MISMATCH ->
            BackupRestoreDiscoveryResult.RecoveryRequired(
                BackupRestoreDiscoveryResult.Reason.SCHEMA_MISMATCH,
            )
        TaffyBackupRestoreWindow.DISCOVERY_OUTCOME_UNKNOWN ->
            BackupRestoreDiscoveryResult.RecoveryRequired(
                BackupRestoreDiscoveryResult.Reason.OUTCOME_UNKNOWN,
            )
        TaffyBackupRestoreWindow.DISCOVERY_CUSTODY_AMBIGUOUS ->
            BackupRestoreDiscoveryResult.RecoveryRequired(
                BackupRestoreDiscoveryResult.Reason.CUSTODY_AMBIGUOUS,
            )
        TaffyBackupRestoreWindow.DISCOVERY_PUBLISHED -> BackupRestoreDiscoveryResult.AlreadyKept
        TaffyBackupRestoreWindow.DISCOVERY_VERIFIED_DELETED ->
            BackupRestoreDiscoveryResult.AlreadyDiscarded
        TaffyBackupRestoreWindow.DISCOVERY_UNAVAILABLE -> BackupRestoreDiscoveryResult.Unavailable
        else -> return ProjectedRestoreDiscovery.Malformed
    }
    return ProjectedRestoreDiscovery.Observation(result)
}

private fun decodeSummary(
    targetProfileLabel: String,
    suppliedCounts: IntArray,
    hasConflicts: Boolean,
    canStage: Boolean,
): BackupRestoreSummary? {
    if (!validTargetLabel(targetProfileLabel)) return null
    val supplied = suppliedCounts
    if (supplied.size !in RESTORE_CLASS_WIDTH..(RESTORE_CLASS_WIDTH * 6) ||
        supplied.size % RESTORE_CLASS_WIDTH != 0
    ) return null
    val flat = supplied.copyOf()
    var previousOrdinal = -1
    var aggregate = 0L
    var conflicts = false
    var unsupportedActions = false
    val rows = ArrayList<BackupRestoreClassSummary>(flat.size / RESTORE_CLASS_WIDTH)
    for (offset in flat.indices step RESTORE_CLASS_WIDTH) {
        val contentClass = flat[offset].toContentClass() ?: return null
        if (contentClass.ordinal <= previousOrdinal) return null
        previousOrdinal = contentClass.ordinal
        val counts = flat.copyOfRange(offset + 1, offset + RESTORE_CLASS_WIDTH)
        if (counts.any { it < 0 }) return null
        aggregate += counts.sumOf { it.toLong() }
        if (aggregate > MAX_RESTORE_ACTION_COUNT) return null
        conflicts = conflicts || counts[5] > 0
        unsupportedActions = unsupportedActions || counts[2] > 0 || counts[3] > 0 ||
            counts[4] > 0 || counts[5] > 0
        rows += BackupRestoreClassSummary(
            contentClass,
            createCount = counts[0],
            deletionCount = counts[1],
            alreadyPresentCount = counts[2],
            keepNewerCount = counts[3],
            blockedCount = counts[4],
            conflictCount = counts[5],
        )
    }
    if (hasConflicts != conflicts) return null
    return BackupRestoreSummary(
        targetProfileLabel = targetProfileLabel,
        selectedClasses = rows.toList(),
        hasConflicts = hasConflicts,
        canStage = canStage && !unsupportedActions,
    )
}

internal fun validTargetLabel(label: String): Boolean =
    label.isNotEmpty() && label == label.trim() && label.length <= 40 &&
        label.none { it < ' ' || it == '\u007f' }

private fun Int.toContentClass(): BackupWindowHost.ContentClass? = when (this) {
    BackupRecordKind.ASSISTANT_CONFIGURATION -> BackupWindowHost.ContentClass.ASSISTANT_CONFIGURATION
    BackupRecordKind.SAVED_WORKSPACE -> BackupWindowHost.ContentClass.SAVED_WORKSPACES
    BackupRecordKind.LIBRARY_ENTRY -> BackupWindowHost.ContentClass.LIBRARY
    BackupRecordKind.MEMORY_RECORD -> BackupWindowHost.ContentClass.MEMORY
    BackupRecordKind.USER_AUTHORED_SKILL -> BackupWindowHost.ContentClass.USER_AUTHORED_SKILLS
    BackupRecordKind.LEARNED_PROCEDURE -> BackupWindowHost.ContentClass.LEARNED_PROCEDURES
    else -> null
}

internal fun BackupRestoreResolutionChoice.toNative() = when (this) {
    BackupRestoreResolutionChoice.ACCEPT -> TaffyBackupRestoreWindow.RESTORE_ACCEPT
    BackupRestoreResolutionChoice.DISCARD -> TaffyBackupRestoreWindow.RESTORE_DISCARD
}

internal fun BackupRestoreSummary.defensiveCopy() = copy(
    selectedClasses = selectedClasses.map { it.copy() },
)

internal fun BackupRestoreResolutionResult.keepsHiddenCandidate() = when (this) {
    BackupRestoreResolutionResult.DEFINITELY_NOT_COMPLETED,
    BackupRestoreResolutionResult.REFUSED,
    BackupRestoreResolutionResult.UNAVAILABLE,
    -> true
    BackupRestoreResolutionResult.PUBLISHED,
    BackupRestoreResolutionResult.VERIFIED_DELETED,
    BackupRestoreResolutionResult.RECOVERY_REQUIRED,
    -> false
}

internal class ResultClaim {
    private var claimed = false

    @Synchronized
    fun claim(): Boolean {
        if (claimed) return false
        claimed = true
        return true
    }
}

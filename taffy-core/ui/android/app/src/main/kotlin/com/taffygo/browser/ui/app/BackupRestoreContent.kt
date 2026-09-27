// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.app

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.LiveRegionMode
import androidx.compose.ui.semantics.liveRegion
import androidx.compose.ui.semantics.semantics
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.ui.TaffyDangerButton
import com.taffygo.browser.ui.core.ui.TaffyPrimaryButton
import com.taffygo.browser.ui.core.ui.TaffySecondaryButton
import com.taffygo.browser.ui.core.ui.taffyString

/** Bounded counts and explicit choices only; a summary never carries a native restore handle. */
@Composable
internal fun BackupRestoreContent(
    state: BackupRestoreUiState,
    resumed: Boolean,
    onIntent: (BackupIntent) -> Unit,
) {
    Column(
        verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.snug),
        modifier = Modifier.testTag("backup-restore-review"),
    ) {
        Text(
            taffyString(state.recoveryReason?.let(::backupRecoveryReasonLabel) ?: backupRestoreStepLabel(state.step)),
            modifier = Modifier.testTag("backup-restore-progress").semantics { liveRegion = LiveRegionMode.Polite },
        )
        state.summary?.let { RestoreSummaryContent(it) }
        if (state.recovered && state.summary != null) Text(taffyString(R.string.taffy_backup_recovery_saved_review))
        if (state.choiceNotCompleted) Text(
            taffyString(R.string.taffy_backup_restore_choice_not_completed),
            modifier = Modifier.testTag("backup-restore-choice-not-completed")
                .semantics { liveRegion = LiveRegionMode.Polite },
        )
        when (state.step) {
            BackupRestoreUiState.Step.REVIEW -> {
                if (state.summary?.hasConflicts == true) Text(taffyString(R.string.taffy_backup_restore_conflict_help))
                TaffyPrimaryButton(
                    label = taffyString(R.string.taffy_backup_restore_prepare),
                    onClick = { onIntent(BackupIntent.StageRestore) },
                    enabled = resumed && state.summary?.canStage == true && state.summary?.hasConflicts == false,
                    testTag = "backup-restore-stage",
                )
            }
            BackupRestoreUiState.Step.STAGED -> TaffyPrimaryButton(
                label = taffyString(R.string.taffy_backup_restore_commit),
                onClick = { onIntent(BackupIntent.CommitRestore) },
                enabled = resumed,
                testTag = "backup-restore-commit",
            )
            BackupRestoreUiState.Step.CANDIDATE, BackupRestoreUiState.Step.CLEANUP_REQUIRED -> {
                if (state.step == BackupRestoreUiState.Step.CANDIDATE) TaffyPrimaryButton(
                    label = taffyString(R.string.taffy_backup_restore_keep),
                    onClick = { onIntent(BackupIntent.AcceptRestore) },
                    enabled = resumed,
                    testTag = "backup-restore-keep",
                )
                TaffyDangerButton(
                    label = taffyString(R.string.taffy_backup_restore_discard),
                    onClick = { onIntent(BackupIntent.RequestDiscardRestore) },
                    enabled = resumed,
                    testTag = "backup-restore-discard-request",
                )
            }
            BackupRestoreUiState.Step.CONFIRM_DISCARD -> {
                Text(taffyString(R.string.taffy_backup_restore_discard_help))
                TaffyDangerButton(
                    label = taffyString(R.string.taffy_backup_restore_discard),
                    onClick = { onIntent(BackupIntent.DiscardRestore) },
                    enabled = resumed,
                    testTag = "backup-restore-discard-confirm",
                )
                TaffySecondaryButton(
                    label = taffyString(R.string.taffy_backup_restore_back_to_review),
                    onClick = { onIntent(BackupIntent.BackToRestoreReview) },
                    enabled = resumed,
                    testTag = "backup-restore-discard-cancel",
                )
            }
            else -> Unit
        }
    }
}

@Composable
private fun RestoreSummaryContent(summary: BackupRestoreSummary) {
    Text(taffyString(R.string.taffy_backup_restore_target_named, summary.targetProfileLabel))
    Text(taffyString(R.string.taffy_backup_restore_selection), style = TaffyTheme.typography.body)
    summary.selectedClasses.forEach { row ->
        val total = row.createCount.toLong() + row.deletionCount + row.alreadyPresentCount +
            row.keepNewerCount + row.blockedCount + row.conflictCount
        Text(
            taffyString(R.string.taffy_backup_restore_count, taffyString(backupContentLabel(row.contentClass)), total),
            modifier = Modifier.testTag("backup-restore-class-${row.contentClass.name}"),
        )
    }
    Text(taffyString(R.string.taffy_backup_restore_creates, summary.selectedClasses.sumOf { it.createCount.toLong() }))
    Text(taffyString(R.string.taffy_backup_restore_deletions, summary.selectedClasses.sumOf { it.deletionCount.toLong() }))
    Text(taffyString(R.string.taffy_backup_restore_unchanged, summary.selectedClasses.sumOf { it.alreadyPresentCount.toLong() }))
    Text(taffyString(R.string.taffy_backup_restore_newer, summary.selectedClasses.sumOf { it.keepNewerCount.toLong() }))
    Text(taffyString(R.string.taffy_backup_restore_blocked, summary.selectedClasses.sumOf { it.blockedCount.toLong() }))
    Text(taffyString(R.string.taffy_backup_restore_conflicts, summary.selectedClasses.sumOf { it.conflictCount.toLong() }))
}

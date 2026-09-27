// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.app

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.selection.toggleable
import androidx.compose.material3.Checkbox
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.LiveRegionMode
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.liveRegion
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.unit.dp
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyGroupedCard
import com.taffygo.browser.ui.core.ui.TaffyLazyScreen
import com.taffygo.browser.ui.core.ui.TaffyPrimaryButton
import com.taffygo.browser.ui.core.ui.TaffySecondaryButton
import com.taffygo.browser.ui.core.ui.taffyString

/** Stateless content; only the six implemented storage families can be selected. */
@Composable
fun BackupContent(
    state: BackupUiState,
    onIntent: (BackupIntent) -> Unit,
    modifier: Modifier = Modifier,
    onBack: (() -> Unit)? = null,
) {
    TaffyLazyScreen(
        destination = TaffyDestination.Backup,
        title = taffyString(R.string.taffy_backup_title),
        onBack = onBack,
        modifier = modifier,
        listModifier = Modifier.testTag("backup-list"),
    ) {
        item(key = "explanation") {
            Column(verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.snug)) {
                Text(taffyString(R.string.taffy_backup_explanation), style = TaffyTheme.typography.body)
                Text(
                    taffyString(R.string.taffy_backup_exclusions),
                    style = TaffyTheme.typography.detail,
                    color = TaffyTheme.colors.textSecondary,
                )
            }
        }
        item(key = "selection") {
            TaffyGroupedCard {
                BackupWindowHost.ContentClass.entries.forEach { content ->
                    Row(
                        modifier = Modifier.fillMaxWidth().heightIn(min = 48.dp)
                            .testTag("backup-select-${content.name}")
                            .toggleable(
                                value = content in state.selection,
                                enabled = state.resumed && !state.busy,
                                role = Role.Checkbox,
                                onValueChange = { onIntent(BackupIntent.Toggle(content)) },
                            ).padding(TaffyTheme.spacing.snug),
                        verticalAlignment = Alignment.CenterVertically,
                    ) {
                        Checkbox(
                            checked = content in state.selection,
                            onCheckedChange = null,
                            enabled = state.resumed && !state.busy,
                        )
                        Text(taffyString(backupContentLabel(content)), modifier = Modifier.weight(1f))
                    }
                }
            }
        }
        item(key = "create") {
            TaffyPrimaryButton(
                label = taffyString(R.string.taffy_backup_create),
                onClick = { onIntent(BackupIntent.Create) },
                enabled = state.canCreate,
                testTag = "backup-create",
            )
        }
        item(key = "check") {
            Text(
                taffyString(R.string.taffy_backup_check_help),
                style = TaffyTheme.typography.detail,
                color = TaffyTheme.colors.textSecondary,
            )
            TaffySecondaryButton(
                label = taffyString(R.string.taffy_backup_check),
                onClick = { onIntent(BackupIntent.Check) },
                enabled = state.canCheck,
                testTag = "backup-check",
            )
        }
        if (state.restoreOffered) {
            item(key = "restore") {
                Text(taffyString(R.string.taffy_backup_restore_help), style = TaffyTheme.typography.detail)
                TaffySecondaryButton(
                    label = taffyString(R.string.taffy_backup_restore),
                    onClick = { onIntent(BackupIntent.Restore) },
                    enabled = state.canCheck,
                    testTag = "backup-restore",
                )
            }
            item(key = "interrupted-restore") {
                Text(taffyString(R.string.taffy_backup_recovery_help), style = TaffyTheme.typography.detail)
                TaffySecondaryButton(
                    label = taffyString(R.string.taffy_backup_recovery_review),
                    onClick = { onIntent(BackupIntent.ReviewInterruptedRestore) },
                    enabled = state.canCheckRecovery,
                    testTag = "backup-recovery-review",
                )
            }
        }
        if (state.step == BackupUiState.Step.RESTORE_IMPORTED) item(key = "restore-imported") {
            val profileLabel = state.restoreName ?: taffyString(R.string.taffy_backup_restore_profile_name)
            Text(taffyString(R.string.taffy_backup_restore_file_checked))
            OutlinedTextField(
                value = profileLabel,
                onValueChange = { onIntent(BackupIntent.RestoreNameChanged(it)) },
                label = { Text(taffyString(R.string.taffy_backup_restore_name_label)) },
                supportingText = { Text(taffyString(
                    R.string.taffy_backup_restore_name_count,
                    profileLabel.length, BackupUiState.MAXIMUM_RESTORE_NAME,
                )) },
                singleLine = true,
                enabled = state.resumed,
                isError = !isBackupRestoreNameReady(profileLabel),
                modifier = Modifier.fillMaxWidth().testTag("backup-restore-name"),
            )
            TaffyPrimaryButton(
                label = taffyString(R.string.taffy_backup_restore_review),
                onClick = { onIntent(BackupIntent.ReviewRestore(profileLabel)) },
                enabled = state.resumed && isBackupRestoreNameReady(profileLabel),
                testTag = "backup-restore-plan",
            )
        }
        state.restore?.let { restore -> item(key = "restore-review") {
            BackupRestoreContent(restore, state.resumed, onIntent)
        } }
        item(key = "choose-deletion") {
            Text(taffyString(R.string.taffy_backup_delete_choose_help), style = TaffyTheme.typography.detail)
            TaffySecondaryButton(
                label = taffyString(R.string.taffy_backup_delete_choose),
                onClick = { onIntent(BackupIntent.ChooseBackupForDeletion) },
                enabled = state.canCheck,
                testTag = "backup-delete-choose",
            )
        }
        state.documentCleanup?.let { cleanup -> item(key = "document-cleanup") {
            BackupDocumentCleanupContent(cleanup, state.cleanupSource, state.documentName, state.resumed, onIntent)
        } }
        if (state.step != BackupUiState.Step.IDLE && state.restore == null &&
            state.step != BackupUiState.Step.RESTORE_IMPORTED
        ) item(key = "progress") {
            Text(
                taffyString(backupStepLabel(state.step)),
                modifier = Modifier.testTag("backup-progress").semantics { liveRegion = LiveRegionMode.Polite },
            )
        }
        if (state.canCancel) item(key = "cancel") {
            TaffySecondaryButton(
                label = taffyString(R.string.taffy_backup_key_cancel),
                onClick = { onIntent(BackupIntent.Cancel) },
                testTag = "backup-cancel",
            )
        }
        if (state.notice != BackupUiState.Notice.NONE) item(key = "notice") {
            Text(
                taffyString(backupNoticeLabel(state.notice)),
                modifier = Modifier.testTag("backup-notice").semantics { liveRegion = LiveRegionMode.Polite },
            )
            TaffySecondaryButton(
                label = taffyString(R.string.taffy_backup_dismiss),
                onClick = { onIntent(BackupIntent.DismissNotice) },
                testTag = "backup-dismiss",
            )
        }
    }
}

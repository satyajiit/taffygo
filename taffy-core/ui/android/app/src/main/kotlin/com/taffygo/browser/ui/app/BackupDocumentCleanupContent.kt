// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.app

import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.LiveRegionMode
import androidx.compose.ui.semantics.liveRegion
import androidx.compose.ui.semantics.semantics
import com.taffygo.browser.ui.core.ui.TaffySecondaryButton
import com.taffygo.browser.ui.core.ui.taffyString

/** No document identifier is displayed or supplied back by a click. */
@Composable
internal fun BackupDocumentCleanupContent(
    step: BackupDocumentCleanupStep,
    source: BackupUiState.CleanupSource?,
    displayName: String?,
    resumed: Boolean,
    onIntent: (BackupIntent) -> Unit,
) {
    val selected = source == BackupUiState.CleanupSource.SELECTED
    if (selected && displayName != null) {
        Text(taffyString(R.string.taffy_backup_delete_selected_name, displayName),
            modifier = Modifier.testTag("backup-delete-selected-name"))
    }
    when (step) {
        BackupDocumentCleanupStep.AVAILABLE -> {
            Text(taffyString(if (selected) R.string.taffy_backup_delete_selected_help
                else R.string.taffy_backup_copy_cleanup_help))
            TaffySecondaryButton(
                label = taffyString(if (selected) R.string.taffy_backup_delete_selected_request
                    else R.string.taffy_backup_copy_delete_request),
                onClick = { onIntent(BackupIntent.RequestDeleteCopy) },
                enabled = resumed,
                testTag = "backup-copy-delete-request",
            )
        }
        BackupDocumentCleanupStep.CONFIRMING -> {
            Text(taffyString(if (selected) R.string.taffy_backup_delete_selected_warning
                else R.string.taffy_backup_copy_delete_warning))
            Text(taffyString(R.string.taffy_backup_copy_retention))
            TaffySecondaryButton(
                label = taffyString(R.string.taffy_backup_copy_delete_confirm),
                onClick = { onIntent(BackupIntent.DeleteDocumentCopy) },
                enabled = resumed,
                testTag = "backup-copy-delete-confirm",
            )
            TaffySecondaryButton(
                label = taffyString(R.string.taffy_backup_copy_keep),
                onClick = { onIntent(BackupIntent.KeepDocumentCopy) },
                enabled = resumed,
                testTag = "backup-copy-keep",
            )
        }
        BackupDocumentCleanupStep.DELETING -> Text(
            taffyString(R.string.taffy_backup_copy_deleting),
            modifier = Modifier.testTag("backup-copy-deleting").semantics { liveRegion = LiveRegionMode.Polite },
        )
    }
}

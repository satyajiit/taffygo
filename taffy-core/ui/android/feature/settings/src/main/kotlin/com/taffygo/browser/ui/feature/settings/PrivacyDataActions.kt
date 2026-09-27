// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.ui.TaffyBottomSheet
import com.taffygo.browser.ui.core.ui.TaffyDangerButton
import com.taffygo.browser.ui.core.ui.TaffyIcon
import com.taffygo.browser.ui.core.ui.TaffyInfoTile
import com.taffygo.browser.ui.core.ui.TaffySecondaryButton
import com.taffygo.browser.ui.core.ui.taffyString

/** Whole-profile actions and their exact, user-verifiable outcomes. */
@Composable
internal fun PrivacyDataActions(
    state: PrivacyUiState,
    onIntent: (PrivacyIntent) -> Unit,
) {
    Column(
        modifier = Modifier.fillMaxWidth(),
        verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.tight),
    ) {
        SettingsHomeEyebrow(title = taffyString(R.string.taffy_privacy_copy_heading))
        TaffyInfoTile(testTag = PRIVACY_EXPORT_DELETE_TEST_TAG) {
            Column(
                modifier = Modifier.fillMaxWidth(),
                verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.tight),
            ) {
                SettingsGlyph(TaffyIcon.Export)
                PrivacyActionCopy(R.string.taffy_privacy_workspaces_stay)
                PrivacyActionCopy(R.string.taffy_privacy_export_scope)
                PrivacyActionCopy(R.string.taffy_privacy_external_copies)
                PrivacyActionStatus(state)
                TaffySecondaryButton(
                    label = taffyString(R.string.taffy_privacy_export),
                    onClick = { onIntent(PrivacyIntent.RequestExport) },
                    enabled = state.canStartExport,
                    testTag = PRIVACY_EXPORT_TEST_TAG,
                )
                TaffyDangerButton(
                    label = taffyString(R.string.taffy_privacy_delete),
                    onClick = { onIntent(PrivacyIntent.RequestDeleteEverything) },
                    enabled = state.canStartDeletion,
                    testTag = PRIVACY_DELETE_TEST_TAG,
                )
            }
        }
    }
}

@Composable
private fun PrivacyActionCopy(resource: Int) {
    Text(
        text = taffyString(resource),
        style = TaffyTheme.typography.detail,
        color = TaffyTheme.colors.textSecondary,
    )
}

@Composable
private fun PrivacyActionStatus(state: PrivacyUiState) {
    privacyExportStatusMessage(state.exportStatus)?.let { message ->
        Text(
            text = taffyString(message),
            style = TaffyTheme.typography.detail,
            color = if (state.exportStatus == PrivacyUiState.ExportStatus.FAILED) {
                TaffyTheme.colors.danger
            } else {
                TaffyTheme.colors.textSecondary
            },
            modifier = Modifier.testTag(PRIVACY_EXPORT_STATUS_TEST_TAG),
        )
    }
    privacyDeletionStatusMessage(state.deletionStatus)?.let { message ->
        Text(
            text = taffyString(message),
            style = TaffyTheme.typography.detail,
            color = if (state.deletionStatus == PrivacyUiState.DeletionStatus.FAILED) {
                TaffyTheme.colors.danger
            } else {
                TaffyTheme.colors.textSecondary
            },
            modifier = Modifier.testTag(PRIVACY_DELETE_STATUS_TEST_TAG),
        )
    }
}

@Composable
internal fun PrivacyDeleteConfirmation(
    state: PrivacyUiState,
    onIntent: (PrivacyIntent) -> Unit,
) {
    if (state.deletionStatus != PrivacyUiState.DeletionStatus.CONFIRMING) return
    TaffyBottomSheet(
        title = taffyString(R.string.taffy_privacy_delete_confirm_title),
        onDismissRequest = { onIntent(PrivacyIntent.DismissDeleteConfirmation) },
        testTag = PRIVACY_DELETE_CONFIRM_TEST_TAG,
    ) {
        PrivacyActionCopy(R.string.taffy_privacy_delete_confirm_body)
        PrivacyActionCopy(R.string.taffy_privacy_delete_external_limit)
        TaffyDangerButton(
            label = taffyString(R.string.taffy_privacy_delete_confirm),
            onClick = { onIntent(PrivacyIntent.ConfirmDeleteEverything) },
            enabled = state.canStartDeletion,
            testTag = PRIVACY_DELETE_CONFIRM_ACTION_TEST_TAG,
        )
        TaffySecondaryButton(
            label = taffyString(R.string.taffy_privacy_delete_keep),
            onClick = { onIntent(PrivacyIntent.DismissDeleteConfirmation) },
        )
    }
}

private fun privacyExportStatusMessage(status: PrivacyUiState.ExportStatus): Int? = when (status) {
    PrivacyUiState.ExportStatus.IDLE -> null
    PrivacyUiState.ExportStatus.CHOOSING_DESTINATION -> R.string.taffy_privacy_export_choosing
    PrivacyUiState.ExportStatus.WRITING -> R.string.taffy_privacy_export_writing
    PrivacyUiState.ExportStatus.SUCCEEDED -> R.string.taffy_privacy_export_succeeded
    PrivacyUiState.ExportStatus.FAILED -> R.string.taffy_privacy_export_failed
    PrivacyUiState.ExportStatus.CANCELLED -> R.string.taffy_privacy_export_cancelled
}

private fun privacyDeletionStatusMessage(status: PrivacyUiState.DeletionStatus): Int? =
    when (status) {
        PrivacyUiState.DeletionStatus.IDLE,
        PrivacyUiState.DeletionStatus.CONFIRMING,
        -> null
        PrivacyUiState.DeletionStatus.STARTING -> R.string.taffy_privacy_delete_starting
        PrivacyUiState.DeletionStatus.STARTED -> R.string.taffy_privacy_delete_started
        PrivacyUiState.DeletionStatus.FAILED -> R.string.taffy_privacy_delete_failed
    }

const val PRIVACY_EXPORT_DELETE_TEST_TAG: String = "privacy_export_delete"
const val PRIVACY_EXPORT_TEST_TAG: String = "privacy_export"
const val PRIVACY_DELETE_TEST_TAG: String = "privacy_delete"
const val PRIVACY_EXPORT_STATUS_TEST_TAG: String = "privacy_export_status"
const val PRIVACY_DELETE_STATUS_TEST_TAG: String = "privacy_delete_status"
const val PRIVACY_DELETE_CONFIRM_TEST_TAG: String = "privacy_delete_confirm"
const val PRIVACY_DELETE_CONFIRM_ACTION_TEST_TAG: String = "privacy_delete_confirm_action"

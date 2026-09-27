// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.workspaces

import androidx.compose.material3.AlertDialog
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import com.taffygo.browser.ui.core.model.WorkspaceDeletionPreview
import com.taffygo.browser.ui.core.ui.TaffyDangerButton
import com.taffygo.browser.ui.core.ui.TaffyPrimaryButton
import com.taffygo.browser.ui.core.ui.TaffySecondaryButton
import com.taffygo.browser.ui.core.ui.taffyString
import taffy.core_api.MAX_WORKSPACE_DISPLAY_NAME_BYTES

/** Shared exact-revision display-name editor for Workspaces and Library. */
@Composable
internal fun WorkspaceRenameDialog(
    currentName: String,
    onDismiss: () -> Unit,
    onConfirm: (String) -> Unit,
) {
    var draft by remember(currentName) { mutableStateOf(currentName) }
    val displayName = draft.trim()
    val valid = displayName.isNotEmpty() &&
        displayName.encodeToByteArray().size <= MAX_WORKSPACE_DISPLAY_NAME_BYTES &&
        displayName.none(Char::isISOControl)
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text(taffyString(R.string.taffy_workspace_rename_title)) },
        text = {
            OutlinedTextField(
                value = draft,
                onValueChange = { draft = it },
                label = { Text(taffyString(R.string.taffy_workspace_rename_label)) },
                singleLine = true,
            )
        },
        confirmButton = {
            TaffyPrimaryButton(
                label = taffyString(R.string.taffy_workspace_rename_confirm),
                onClick = { onConfirm(displayName) },
                enabled = valid,
                testTag = WORKSPACE_RENAME_CONFIRM_TEST_TAG,
            )
        },
        dismissButton = {
            TaffySecondaryButton(
                label = taffyString(R.string.taffy_workspace_lifecycle_cancel),
                onClick = onDismiss,
            )
        },
    )
}

/** Shared content-free deletion preview and confirmation. */
@Composable
internal fun WorkspaceDeleteDialog(
    preview: WorkspaceDeletionPreview,
    onDismiss: () -> Unit,
    onConfirm: () -> Unit,
) {
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text(taffyString(R.string.taffy_workspace_delete_title)) },
        text = {
            Text(
                taffyString(
                    R.string.taffy_workspace_delete_body,
                    preview.sources.toLong(),
                    preview.facts.toLong(),
                    preview.artifactMetadata.toLong(),
                    preview.derivedIndexes.toLong(),
                ),
            )
        },
        confirmButton = {
            TaffyDangerButton(
                label = taffyString(R.string.taffy_workspace_delete_confirm),
                onClick = onConfirm,
                testTag = WORKSPACE_DELETE_CONFIRM_TEST_TAG,
            )
        },
        dismissButton = {
            TaffySecondaryButton(
                label = taffyString(R.string.taffy_workspace_lifecycle_cancel),
                onClick = onDismiss,
            )
        },
    )
}

const val WORKSPACE_RENAME_CONFIRM_TEST_TAG: String = "workspace_rename_confirm"
const val WORKSPACE_DELETE_CONFIRM_TEST_TAG: String = "workspace_delete_confirm"

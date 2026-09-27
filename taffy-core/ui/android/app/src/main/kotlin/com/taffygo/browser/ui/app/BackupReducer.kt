// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.app

/** Pure local choices, not a second backup protocol or a grant of authority. */
internal fun reduceBackup(state: BackupUiState, intent: BackupIntent): BackupUiState = when (intent) {
    is BackupIntent.Toggle -> if (state.busy || !state.resumed) state else state.copy(
        selection = if (intent.content in state.selection) {
            state.selection - intent.content
        } else {
            state.selection + intent.content
        },
        notice = BackupUiState.Notice.NONE,
    )
    is BackupIntent.RestoreNameChanged -> if (
        !state.resumed || state.step != BackupUiState.Step.RESTORE_IMPORTED ||
        intent.name.length > BackupUiState.MAXIMUM_RESTORE_NAME
    ) state else state.copy(restoreName = intent.name)
    BackupIntent.DismissNotice -> state.copy(notice = BackupUiState.Notice.NONE)
    else -> state
}

/** Input feedback only. Native independently validates the name and owns profile identity. */
internal fun isBackupRestoreNameReady(name: String): Boolean =
    name.isNotBlank() && name.length <= BackupUiState.MAXIMUM_RESTORE_NAME &&
        name == name.trim() && name.none { it < ' ' || it == '\u007f' }

internal fun backupWriteNotice(result: BackupDocumentTransfer.WriteResult): BackupUiState.Notice =
    when (result) {
        BackupDocumentTransfer.WriteResult.VERIFIED -> BackupUiState.Notice.SAVED
        BackupDocumentTransfer.WriteResult.INCOMPLETE -> BackupUiState.Notice.INCOMPLETE
        BackupDocumentTransfer.WriteResult.UNAVAILABLE -> BackupUiState.Notice.UNAVAILABLE
    }

internal fun backupImportNotice(result: BackupDocumentImport.ImportStatus): BackupUiState.Notice =
    when (result) {
        BackupDocumentImport.ImportStatus.VERIFIED -> BackupUiState.Notice.CHECKED_ONLY
        BackupDocumentImport.ImportStatus.REFUSED -> BackupUiState.Notice.IMPORT_REFUSED
        BackupDocumentImport.ImportStatus.UNAVAILABLE -> BackupUiState.Notice.UNAVAILABLE
    }

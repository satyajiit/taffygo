// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.app

/** The person's screen actions; archive and restore policy remain native. */
sealed interface BackupIntent {
    data class Toggle(val content: BackupWindowHost.ContentClass) : BackupIntent
    data object Create : BackupIntent
    data object Check : BackupIntent
    data object Restore : BackupIntent
    data object ReviewInterruptedRestore : BackupIntent
    data class RestoreNameChanged(val name: String) : BackupIntent
    data class ReviewRestore(val targetProfileLabel: String) : BackupIntent
    data object StageRestore : BackupIntent
    data object CommitRestore : BackupIntent
    data object AcceptRestore : BackupIntent
    data object RequestDiscardRestore : BackupIntent
    data object DiscardRestore : BackupIntent
    data object BackToRestoreReview : BackupIntent
    data object RequestDeleteCopy : BackupIntent
    data object ChooseBackupForDeletion : BackupIntent
    data object KeepDocumentCopy : BackupIntent
    data object DeleteDocumentCopy : BackupIntent
    data object Cancel : BackupIntent
    data object DismissNotice : BackupIntent
}

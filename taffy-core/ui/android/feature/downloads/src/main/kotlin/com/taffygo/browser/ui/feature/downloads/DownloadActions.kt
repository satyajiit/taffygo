// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.downloads

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.model.DownloadAction
import com.taffygo.browser.ui.core.model.DownloadRecord
import com.taffygo.browser.ui.core.ui.TaffyDangerButton
import com.taffygo.browser.ui.core.ui.TaffySecondaryButton
import com.taffygo.browser.ui.core.ui.taffyString

@Composable
internal fun DownloadActions(
    download: DownloadRecord,
    onIntent: (DownloadsIntent) -> Unit,
    onAskConfirmation: (PendingDownloadAction) -> Unit,
) {
    Column(verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.step)) {
        if (DownloadAction.PAUSE in download.allowedActions) {
            ActionButton(
                R.string.taffy_downloads_pause,
                download.fileName,
                "$PAUSE_TEST_TAG_PREFIX${download.id.value}",
            ) {
                onIntent(DownloadsIntent.Pause(download.id))
            }
        }
        if (DownloadAction.RESUME in download.allowedActions) {
            ActionButton(
                R.string.taffy_downloads_resume,
                download.fileName,
                "$RESUME_TEST_TAG_PREFIX${download.id.value}",
            ) {
                onIntent(DownloadsIntent.Resume(download.id))
            }
        }
        if (DownloadAction.CANCEL in download.allowedActions) {
            ActionButton(
                R.string.taffy_downloads_cancel,
                download.fileName,
                "$CANCEL_TEST_TAG_PREFIX${download.id.value}",
            ) {
                onAskConfirmation(PendingDownloadAction(download, DownloadAction.CANCEL))
            }
        }
        if (DownloadAction.OPEN in download.allowedActions) {
            ActionButton(
                R.string.taffy_downloads_open,
                download.fileName,
                "$OPEN_TEST_TAG_PREFIX${download.id.value}",
            ) {
                onIntent(DownloadsIntent.Open(download.id))
            }
        }
        if (DownloadAction.SHARE in download.allowedActions) {
            ActionButton(
                R.string.taffy_downloads_share,
                download.fileName,
                "$SHARE_TEST_TAG_PREFIX${download.id.value}",
            ) {
                onIntent(DownloadsIntent.Share(download.id))
            }
        }
        if (DownloadAction.REMOVE in download.allowedActions) {
            ActionButton(
                R.string.taffy_downloads_remove,
                download.fileName,
                "$REMOVE_TEST_TAG_PREFIX${download.id.value}",
            ) {
                onAskConfirmation(PendingDownloadAction(download, DownloadAction.REMOVE))
            }
        }
    }
}

@Composable
private fun ActionButton(labelRes: Int, fileName: String, testTag: String, onClick: () -> Unit) {
    val label = taffyString(labelRes)
    val description = taffyString(R.string.taffy_downloads_action_description, label, fileName)
    TaffySecondaryButton(
        label = label,
        onClick = onClick,
        modifier = Modifier.semantics { contentDescription = description },
        testTag = testTag,
    )
}

@Composable
internal fun DownloadActionConfirmation(
    pending: PendingDownloadAction,
    onDismiss: () -> Unit,
    onConfirm: () -> Unit,
) {
    val removing = pending.action == DownloadAction.REMOVE
    AlertDialog(
        onDismissRequest = onDismiss,
        title = {
            Text(
                taffyString(
                    if (removing) R.string.taffy_downloads_remove_confirm_title
                    else R.string.taffy_downloads_cancel_confirm_title,
                ),
            )
        },
        text = {
            Text(
                taffyString(
                    if (removing) R.string.taffy_downloads_remove_confirm_body
                    else R.string.taffy_downloads_cancel_confirm_body,
                    pending.download.fileName,
                ),
            )
        },
        confirmButton = {
            TaffyDangerButton(
                label = taffyString(
                    if (removing) R.string.taffy_downloads_remove
                    else R.string.taffy_downloads_cancel,
                ),
                onClick = onConfirm,
                testTag = DOWNLOAD_CONFIRM_TEST_TAG,
            )
        },
        dismissButton = {
            TaffySecondaryButton(
                label = taffyString(R.string.taffy_downloads_keep),
                onClick = onDismiss,
            )
        },
    )
}

internal data class PendingDownloadAction(
    val download: DownloadRecord,
    val action: DownloadAction,
)

const val PAUSE_TEST_TAG_PREFIX: String = "download_pause_"
const val RESUME_TEST_TAG_PREFIX: String = "download_resume_"
const val CANCEL_TEST_TAG_PREFIX: String = "download_cancel_"
const val OPEN_TEST_TAG_PREFIX: String = "download_open_"
const val SHARE_TEST_TAG_PREFIX: String = "download_share_"
const val REMOVE_TEST_TAG_PREFIX: String = "download_remove_"
const val DOWNLOAD_CONFIRM_TEST_TAG: String = "download_action_confirm"

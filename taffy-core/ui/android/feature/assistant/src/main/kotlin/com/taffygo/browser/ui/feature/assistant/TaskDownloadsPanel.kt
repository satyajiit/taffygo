// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.assistant

import android.text.format.Formatter
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.material3.Icon
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.model.DownloadId
import com.taffygo.browser.ui.core.ui.TaffyIcon
import com.taffygo.browser.ui.core.ui.TaffySecondaryButton
import com.taffygo.browser.ui.core.ui.taffyString

/** A result to open, never a synthetic preview of the person's downloaded document. */
@Composable
internal fun TaskDownloadsPanel(
    state: TaskDownloadsUiState,
    onOpen: (DownloadId) -> Unit,
    onShowDownloads: () -> Unit,
    footerFile: DownloadId? = null,
) {
    val colors = TaffyTheme.colors
    val context = LocalContext.current
    Column(verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.snug)) {
        state.files.forEach { file ->
            val pdf = file.mimeType.equals("application/pdf", ignoreCase = true)
            val failed = state.failed == file.id
            Column(
                modifier = Modifier.fillMaxWidth().clip(TaffyTheme.shapes.card)
                    .background(if (failed) colors.dangerWash else colors.positiveWash).padding(TaffyTheme.spacing.snug)
                    .testTag("$TASK_DOWNLOAD_CARD_TEST_TAG_PREFIX${file.id.value}"),
                verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.snug),
            ) {
                Row(
                    horizontalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.snug),
                    verticalAlignment = Alignment.CenterVertically,
                ) {
                    Box(
                        modifier = Modifier.size(56.dp).clip(TaffyTheme.shapes.card).background(colors.surfaceRaised),
                        contentAlignment = Alignment.Center,
                    ) {
                        Icon(if (failed) TaffyIcon.Warning else TaffyIcon.DownloadSimple,
                            contentDescription = null, tint = if (failed) colors.dangerText else colors.positiveText,
                            modifier = Modifier.size(30.dp))
                    }
                    Column(verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.step)) {
                        Text(
                            text = taffyString(when {
                                failed -> R.string.taffy_task_file_open_failed_title
                                pdf -> R.string.taffy_task_pdf_ready
                                else -> R.string.taffy_task_file_ready
                            }),
                            style = TaffyTheme.typography.title, color = colors.textPrimary,
                        )
                        if (!failed) Text(taffyString(R.string.taffy_task_file_on_device),
                            style = TaffyTheme.typography.detail, color = colors.textSecondary)
                    }
                }
                Text(file.fileName, style = TaffyTheme.typography.label, color = colors.textPrimary,
                    maxLines = 2, overflow = TextOverflow.Ellipsis)
                Text(
                    text = listOf(file.host, Formatter.formatShortFileSize(context, file.downloadedBytes)).filter { it.isNotBlank() }
                        .joinToString(" · "),
                    style = TaffyTheme.typography.caption, color = colors.textSecondary,
                )
                if (failed) {
                    Text(taffyString(R.string.taffy_task_file_open_failed),
                        style = TaffyTheme.typography.detail, color = colors.dangerText)
                    if (file.id != footerFile) TaffySecondaryButton(label = taffyString(R.string.taffy_task_file_show_downloads),
                        onClick = onShowDownloads, testTag = TASK_DOWNLOADS_VIEW_TEST_TAG)
                } else if (file.id != footerFile) TaffySecondaryButton(
                    label = taffyString(when {
                        state.opening == file.id -> R.string.taffy_task_file_opening
                        pdf -> R.string.taffy_task_pdf_open
                        else -> R.string.taffy_task_file_open
                    }),
                    onClick = { onOpen(file.id) },
                    enabled = state.opening == null,
                    testTag = "$TASK_DOWNLOAD_OPEN_TEST_TAG_PREFIX${file.id.value}",
                )
            }
        }
    }
}

const val TASK_DOWNLOAD_CARD_TEST_TAG_PREFIX: String = "task_download_card_"
const val TASK_DOWNLOAD_OPEN_TEST_TAG_PREFIX: String = "task_download_open_"
const val TASK_DOWNLOADS_VIEW_TEST_TAG: String = "task_downloads_view"

// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.downloads

import androidx.compose.foundation.lazy.LazyListScope
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.semantics
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.model.DownloadRecord
import com.taffygo.browser.ui.core.ui.StatusPresentation
import com.taffygo.browser.ui.core.ui.TaffyListRow
import com.taffygo.browser.ui.core.ui.TaffyStatusChip
import com.taffygo.browser.ui.core.ui.taffyString

internal fun LazyListScope.downloadGroups(
    state: DownloadsUiState,
    onIntent: (DownloadsIntent) -> Unit,
    onAskConfirmation: (PendingDownloadAction) -> Unit,
) {
    state.groups.forEach { group ->
        if (state.grouping != DownloadGrouping.NONE) {
            item(key = "header:${group.key}", contentType = "group-header") {
                Text(
                    text = groupTitle(group.title),
                    style = TaffyTheme.typography.title,
                    color = TaffyTheme.colors.textPrimary,
                    modifier = Modifier
                        .testTag("$DOWNLOAD_GROUP_TEST_TAG_PREFIX${group.key}")
                        .semantics { heading() },
                )
            }
        }
        items(
            items = group.downloads,
            key = { it.id.value },
            contentType = { "download" },
        ) { download ->
            DownloadRow(download, onIntent, onAskConfirmation)
        }
    }
}

@Composable
private fun DownloadRow(
    download: DownloadRecord,
    onIntent: (DownloadsIntent) -> Unit,
    onAskConfirmation: (PendingDownloadAction) -> Unit,
) {
    val presentation = StatusPresentation.of(download.state)
    val progress = download.fraction
        ?.let { taffyString(R.string.taffy_downloads_progress, (it * 100).toInt()) }
        ?: taffyString(R.string.taffy_downloads_progress_unknown)
    val source = download.host.ifBlank { taffyString(R.string.taffy_downloads_source_unknown) }
    Column(verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.step)) {
        TaffyListRow(
            title = download.fileName,
            supporting = taffyString(R.string.taffy_downloads_supporting, source, progress),
            accessibleDescription = taffyString(
                R.string.taffy_downloads_description,
                download.fileName,
                source,
                taffyString(presentation.labelRes),
                progress,
            ),
            testTag = "$DOWNLOAD_TEST_TAG_PREFIX${download.id.value}",
            trailing = { TaffyStatusChip(presentation = presentation) },
        )
        if (download.allowedActions.isNotEmpty()) {
            DownloadActions(download, onIntent, onAskConfirmation)
        }
    }
}

@Composable
internal fun groupTitle(title: DownloadGroupTitle): String = when (title) {
    DownloadGroupTitle.All -> ""
    is DownloadGroupTitle.Status -> taffyString(StatusPresentation.of(title.state).labelRes)
    is DownloadGroupTitle.FileType -> taffyString(
        when (title.type) {
            DownloadFileType.DOCUMENT -> R.string.taffy_downloads_type_documents
            DownloadFileType.IMAGE -> R.string.taffy_downloads_type_images
            DownloadFileType.AUDIO -> R.string.taffy_downloads_type_audio
            DownloadFileType.VIDEO -> R.string.taffy_downloads_type_video
            DownloadFileType.ARCHIVE -> R.string.taffy_downloads_type_archives
            DownloadFileType.OTHER -> R.string.taffy_downloads_type_other
        },
    )
    is DownloadGroupTitle.Source -> title.host.ifBlank {
        taffyString(R.string.taffy_downloads_source_unknown)
    }
}

const val DOWNLOAD_TEST_TAG_PREFIX: String = "download_"
const val DOWNLOAD_GROUP_TEST_TAG_PREFIX: String = "download_group_"

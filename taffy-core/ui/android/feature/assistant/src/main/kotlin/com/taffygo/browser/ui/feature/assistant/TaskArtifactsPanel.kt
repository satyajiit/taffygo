// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.assistant

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.lazy.LazyListScope
import androidx.compose.foundation.lazy.itemsIndexed
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.task.TaskArtifactProjection
import com.taffygo.browser.ui.core.ui.TaffyButtonSize
import com.taffygo.browser.ui.core.ui.TaffyListRow
import com.taffygo.browser.ui.core.ui.TaffySecondaryButton
import com.taffygo.browser.ui.core.ui.TaffySectionHeader
import com.taffygo.browser.ui.core.ui.taffyString

/** Reproducible file metadata plus explicit keep, save, and share actions. */
internal fun LazyListScope.taskArtifactsPanel(
    state: TaskViewUiState,
    onIntent: (TaskViewIntent) -> Unit,
) {
    if (state.artifacts.isEmpty()) return
    item(key = "files-heading") {
        TaffySectionHeader(
            title = taffyString(R.string.taffy_task_view_files_title),
            modifier = Modifier.testTag(TASK_FILES_TEST_TAG),
        )
    }
    itemsIndexed(
        items = state.artifacts,
        key = { index, artifact -> "artifact-$index-${artifact.id}" },
    ) { _, artifact ->
        TaskArtifactRow(artifact = artifact, state = state, onIntent = onIntent)
    }
    if (state.artifactExportFailed) {
        item(key = "file-failure") {
            Text(
                text = taffyString(R.string.taffy_task_view_file_failed),
                style = TaffyTheme.typography.detail,
                color = TaffyTheme.colors.danger,
                modifier = Modifier.testTag(TASK_FILE_FAILURE_TEST_TAG),
            )
        }
    }
}

@Composable
private fun TaskArtifactRow(
    artifact: TaskArtifactProjection,
    state: TaskViewUiState,
    onIntent: (TaskViewIntent) -> Unit,
) {
    val exporting = state.artifactExportingId == artifact.id
    val status = taffyString(
        when {
            exporting -> R.string.taffy_task_view_file_preparing
            artifact.accepted -> R.string.taffy_task_view_file_kept
            else -> R.string.taffy_task_view_file_ready
        },
    )
    Column(verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.step)) {
        TaffyListRow(
            title = artifact.suggestedFileName,
            supporting = status,
            accessibleDescription = taffyString(
                R.string.taffy_task_view_file_description,
                artifact.suggestedFileName,
                status,
            ),
            testTag = "$TASK_FILE_TEST_TAG_PREFIX${artifact.id}",
        )
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(
                TaffyTheme.spacing.tight,
                Alignment.End,
            ),
        ) {
            if (!artifact.accepted) {
                TaffySecondaryButton(
                    label = taffyString(R.string.taffy_task_view_file_keep),
                    onClick = { onIntent(TaskViewIntent.AcceptArtifact(artifact.id)) },
                    enabled = state.artifactExportingId == null,
                    size = TaffyButtonSize.COMPACT,
                    testTag = "$TASK_FILE_KEEP_TEST_TAG_PREFIX${artifact.id}",
                )
            } else {
                TaffySecondaryButton(
                    label = taffyString(R.string.taffy_task_view_file_save),
                    onClick = {
                        onIntent(
                            TaskViewIntent.ExportArtifact(
                                artifact.id,
                                TaskViewIntent.ArtifactDestination.CREATE_DOCUMENT,
                            ),
                        )
                    },
                    enabled = state.artifactExportingId == null,
                    loading = exporting,
                    size = TaffyButtonSize.COMPACT,
                    testTag = "$TASK_FILE_SAVE_TEST_TAG_PREFIX${artifact.id}",
                )
                TaffySecondaryButton(
                    label = taffyString(R.string.taffy_task_view_file_share),
                    onClick = {
                        onIntent(
                            TaskViewIntent.ExportArtifact(
                                artifact.id,
                                TaskViewIntent.ArtifactDestination.SHARE,
                            ),
                        )
                    },
                    enabled = state.artifactExportingId == null,
                    size = TaffyButtonSize.COMPACT,
                    testTag = "$TASK_FILE_SHARE_TEST_TAG_PREFIX${artifact.id}",
                )
            }
        }
    }
}

const val TASK_FILES_TEST_TAG: String = "task_view_files"
const val TASK_FILE_TEST_TAG_PREFIX: String = "task_view_file_"
const val TASK_FILE_KEEP_TEST_TAG_PREFIX: String = "task_view_file_keep_"
const val TASK_FILE_SAVE_TEST_TAG_PREFIX: String = "task_view_file_save_"
const val TASK_FILE_SHARE_TEST_TAG_PREFIX: String = "task_view_file_share_"
const val TASK_FILE_FAILURE_TEST_TAG: String = "task_view_file_failure"

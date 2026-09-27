// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.assistant

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.LiveRegionMode
import androidx.compose.ui.semantics.liveRegion
import androidx.compose.ui.semantics.semantics
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.ui.TaffyButtonSize
import com.taffygo.browser.ui.core.ui.TaffyIcon
import com.taffygo.browser.ui.core.ui.TaffySecondaryButton
import com.taffygo.browser.ui.core.ui.taffyString

/** Person-started playback controls for the final answer already on screen. */
@Composable
internal fun TaskAnswerReadAloudControls(
    state: TaskViewUiState,
    onIntent: (TaskViewIntent) -> Unit,
) {
    if (!state.canReadAnswerAloud && state.readAloud == ReadAloudUiState.Idle) return
    val active = state.readAloud == ReadAloudUiState.Preparing ||
        state.readAloud == ReadAloudUiState.Speaking
    Column(verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.tight)) {
        TaffySecondaryButton(
            label = taffyString(
                when (state.readAloud) {
                    ReadAloudUiState.Preparing -> R.string.taffy_task_view_read_aloud_cancel
                    ReadAloudUiState.Speaking -> R.string.taffy_task_view_read_aloud_stop
                    ReadAloudUiState.Idle,
                    ReadAloudUiState.Error,
                    -> R.string.taffy_task_view_read_aloud_start
                },
            ),
            onClick = {
                onIntent(
                    if (active) TaskViewIntent.StopReadAloud else TaskViewIntent.ReadAnswerAloud,
                )
            },
            enabled = active || state.canReadAnswerAloud,
            icon = if (active) TaffyIcon.SpeakerSlash else TaffyIcon.SpeakerHigh,
            size = TaffyButtonSize.COMPACT,
            testTag = READ_ALOUD_BUTTON_TEST_TAG,
        )
        val status = when (state.readAloud) {
            ReadAloudUiState.Idle -> null
            ReadAloudUiState.Preparing -> R.string.taffy_task_view_read_aloud_preparing
            ReadAloudUiState.Speaking -> R.string.taffy_task_view_read_aloud_speaking
            ReadAloudUiState.Error -> R.string.taffy_task_view_read_aloud_error
        }
        status?.let { message ->
            Text(
                text = taffyString(message),
                style = TaffyTheme.typography.detail,
                color = if (state.readAloud == ReadAloudUiState.Error) {
                    TaffyTheme.colors.caution
                } else {
                    TaffyTheme.colors.textSecondary
                },
                modifier = Modifier
                    .semantics { liveRegion = LiveRegionMode.Polite }
                    .testTag(READ_ALOUD_STATUS_TEST_TAG),
            )
        }
    }
}

const val READ_ALOUD_BUTTON_TEST_TAG: String = "task_view_read_aloud"
const val READ_ALOUD_STATUS_TEST_TAG: String = "task_view_read_aloud_status"

// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.assistant

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.LiveRegionMode
import androidx.compose.ui.semantics.liveRegion
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.unit.dp
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.model.TaskDisplayState
import com.taffygo.browser.ui.core.ui.TaffyBrandMark
import com.taffygo.browser.ui.core.ui.taffyString

/** One assistant below all of its pages. No inferred per-tab activity or progress fraction. */
@Composable
internal fun TaskWorkspaceActivity(state: TaskViewUiState, modifier: Modifier = Modifier) {
    val display = state.state ?: return
    if (state.notice != null) return
    val line = when {
        state.hasHandover -> R.string.taffy_task_browser_handover
        state.hasAsk -> R.string.taffy_task_browser_question
        else -> when (display) {
            TaskDisplayState.RUNNING -> R.string.taffy_task_browser_running
            TaskDisplayState.WAITING_FOR_YOU -> R.string.taffy_task_browser_waiting
            TaskDisplayState.PAUSED -> R.string.taffy_task_browser_paused
            TaskDisplayState.DONE -> R.string.taffy_task_browser_done
            TaskDisplayState.PARTLY_DONE -> R.string.taffy_task_browser_partial
            TaskDisplayState.STOPPED -> R.string.taffy_task_browser_stopped
            TaskDisplayState.FAILED -> R.string.taffy_task_browser_failed
        }
    }
    Row(
        modifier = modifier.fillMaxWidth()
            .clip(TaffyTheme.shapes.card)
            .background(TaffyTheme.colors.ribbonTwoWash)
            .padding(TaffyTheme.spacing.snug)
            .testTag(TASK_WORKSPACE_ACTIVITY_TEST_TAG)
            .semantics { liveRegion = LiveRegionMode.Polite },
        horizontalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.snug),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        TaffyBrandMark(size = 28.dp, contentDescription = null)
        Column(verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.step)) {
            Text(
                text = taffyString(R.string.taffy_task_browser_activity),
                style = TaffyTheme.typography.label,
                color = TaffyTheme.colors.textPrimary,
            )
            Text(
                text = taffyString(line),
                style = TaffyTheme.typography.caption,
                color = TaffyTheme.colors.textSecondary,
            )
        }
    }
}

const val TASK_WORKSPACE_ACTIVITY_TEST_TAG: String = "task_workspace_activity"

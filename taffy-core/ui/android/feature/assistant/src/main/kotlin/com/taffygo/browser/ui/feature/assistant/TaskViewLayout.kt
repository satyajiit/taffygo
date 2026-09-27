// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.assistant

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.setValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.model.TabId
import com.taffygo.browser.ui.core.task.TaskAnswerProjection
import com.taffygo.browser.ui.core.ui.TaffyPaneSplit
import com.taffygo.browser.ui.core.ui.TaffyTwoPane

/**
 * Pages and current activity precede the answer. Long facts remain lazy;
 * history expands on request. Wide screens keep pages beside the result.
 */
@Composable
internal fun TaskViewLayout(
    state: TaskViewUiState,
    header: @Composable () -> Unit,
    onIntent: (TaskViewIntent) -> Unit,
    browserState: TaskBrowserUiState,
    onOpenTab: (TabId) -> Unit,
    flowReview: @Composable () -> Unit,
    downloads: @Composable () -> Unit,
    hasTaskDownloads: Boolean,
    modifier: Modifier = Modifier,
    onReportAnswer: ((TaskAnswerProjection) -> Unit)? = null,
) {
    var expandedTimeline by remember(state.taskId) { mutableStateOf(false) }
    if (!TaffyTheme.windowWidth.showsTwoPane) {
        LazyColumn(
            modifier = modifier.fillMaxSize().testTag(TASK_LIST_TEST_TAG),
            verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.snug),
        ) {
            item(key = "header") { header() }
            if (hasTaskDownloads) item(key = "downloads") { downloads() }
            if (browserState.taskId == state.taskId && browserState.tabs.isNotEmpty()) {
                item(key = "task-pages") { TaskBrowserPanel(browserState, onOpenTab) }
                item(key = "task-activity") { TaskWorkspaceActivity(state) }
            }
            taskAnswerPanel(state = state, onIntent = onIntent, onReportAnswer = onReportAnswer)
            if (state.facts.isNotEmpty()) taskOutputPanel(facts = state.facts, sources = state.sources, marks = browserState.siteMarks)
            if (state.sources.isNotEmpty()) taskSourcesPanel(sources = state.sources, marks = browserState.siteMarks)
            if (!hasTaskDownloads || state.timeline.isNotEmpty() || state.notice != null) {
                taskTimelinePanel(state.timeline, state.notice, expandedTimeline) { expandedTimeline = !expandedTimeline }
            }
            item(key = "flow-review") { flowReview() }
            taskArtifactsPanel(state = state, onIntent = onIntent)
        }
        return
    }
    TaffyTwoPane(
        primary = {
            LazyColumn(
                modifier = Modifier.fillMaxSize().testTag(TASK_PRIMARY_LIST_TEST_TAG),
                verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.snug),
            ) {
                item(key = "header") { header() }
                if (hasTaskDownloads) item(key = "downloads") { downloads() }
                if (browserState.taskId == state.taskId && browserState.tabs.isNotEmpty()) {
                    item(key = "task-pages") { TaskBrowserPanel(browserState, onOpenTab) }
                    item(key = "task-activity") { TaskWorkspaceActivity(state) }
                }
                item(key = "flow-review") { flowReview() }
                if (!hasTaskDownloads || state.timeline.isNotEmpty() || state.notice != null) {
                    taskTimelinePanel(state.timeline, state.notice, expandedTimeline) { expandedTimeline = !expandedTimeline }
                }
            }
        },
        secondary = {
            LazyColumn(
                modifier = Modifier.fillMaxSize().testTag(TASK_SECONDARY_LIST_TEST_TAG),
                verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.snug),
            ) {
                taskAnswerPanel(state = state, onIntent = onIntent, onReportAnswer = onReportAnswer)
                if (state.facts.isNotEmpty()) taskOutputPanel(facts = state.facts, sources = state.sources, marks = browserState.siteMarks)
                if (state.sources.isNotEmpty()) taskSourcesPanel(sources = state.sources, marks = browserState.siteMarks)
                taskArtifactsPanel(state = state, onIntent = onIntent)
            }
        },
        split = TaffyPaneSplit.HALF,
        modifier = modifier,
    )
}

const val TASK_LIST_TEST_TAG: String = "task_view_list"
const val TASK_PRIMARY_LIST_TEST_TAG: String = "task_view_primary_list"
const val TASK_SECONDARY_LIST_TEST_TAG: String = "task_view_secondary_list"

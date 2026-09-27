// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.assistant

import android.graphics.Bitmap
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.lazy.LazyListScope
import androidx.compose.foundation.lazy.itemsIndexed
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import com.taffygo.browser.ui.core.model.Fact
import com.taffygo.browser.ui.core.model.SourceRecord
import com.taffygo.browser.ui.core.model.TaskTimelineEntry
import com.taffygo.browser.ui.core.task.TaskAnswerProjection
import com.taffygo.browser.ui.core.ui.TaffyEmptyState
import com.taffygo.browser.ui.core.ui.TaffySectionHeader
import com.taffygo.browser.ui.core.ui.taffyString

/**
 * The three panels of UX spec section 6 that are lists: the timeline, the live
 * source list, and the output.
 *
 * They are here rather than in `TaskViewScreen.kt` because one file never
 * becomes the whole screen — the soft line cap of android-app-architecture
 * section 5 is a design rule, and this is what obeying it looks like.
 */
internal fun LazyListScope.taskTimelinePanel(
    entries: List<TaskTimelineEntry>,
    notice: TaskNotice?,
    expanded: Boolean,
    onToggle: () -> Unit,
) {
    item(key = "timeline-heading") {
        TaffySectionHeader(
            title = taffyString(R.string.taffy_task_view_timeline_title),
            modifier = if (entries.isNotEmpty()) Modifier.testTag(TIMELINE_TEST_TAG) else Modifier,
        )
    }
    // The notice comes first, and it replaces the generic empty state rather
    // than joining it. "Nothing has happened yet" is a promise that something
    // will; where nothing will, that sentence is the untrue half of the screen
    // and printing both would leave a person to pick which one to believe.
    if (notice != null) {
        // The tag goes on a wrapper because TaffyEmptyState tags itself last,
        // and the second testTag on one node replaces the first.
        item(key = "timeline-notice") {
            Column(modifier = Modifier.testTag(TIMELINE_NOTICE_TEST_TAG)) {
                TaffyEmptyState(
                    title = taffyString(noticeTitle(notice)),
                    body = taffyString(noticeBody(notice)),
                )
            }
        }
    }
    if (entries.isEmpty()) {
        if (notice == null) {
            item(key = "timeline-empty") {
                TaffyEmptyState(
                    title = taffyString(R.string.taffy_task_view_timeline_empty_title),
                    body = taffyString(R.string.taffy_task_view_timeline_empty_body),
                )
            }
        }
        return
    }
    itemsIndexed(
        items = if (expanded) entries else entries.take(3),
        key = { index, entry -> "timeline-$index-${entry.sequence}" },
    ) { index, entry -> TaskTimelineStep(entry, newest = index == 0) }
    if (entries.size > 3) item(key = "timeline-toggle") {
        TextButton(onClick = onToggle, modifier = Modifier.testTag(TIMELINE_TOGGLE_TEST_TAG)) {
            Text(taffyString(if (expanded) R.string.taffy_task_results_fewer_steps else R.string.taffy_task_results_all_steps),
                style = TaffyTheme.typography.label, color = TaffyTheme.colors.textPrimary)
        }
    }

}

/** The live, browser-owned source list. */
internal fun LazyListScope.taskSourcesPanel(
    sources: List<SourceRecord>,
    marks: Map<String, Bitmap> = emptyMap(),
) {
    item(key = "sources-heading") {
        TaffySectionHeader(
            title = taffyString(R.string.taffy_task_view_sources_title),
            modifier = if (sources.isNotEmpty()) Modifier.testTag(SOURCES_TEST_TAG) else Modifier,
        )
    }
    if (sources.isEmpty()) {
        item(key = "sources-empty") {
            TaffyEmptyState(
                title = taffyString(R.string.taffy_task_view_sources_empty_title),
                body = taffyString(R.string.taffy_task_view_sources_empty_body),
            )
        }
        return
    }
    itemsIndexed(
        items = sources,
        key = { index, source -> "source-$index-${source.id.value}" },
    ) { index, source -> TaskSourceRow(source, index + 1, marks[source.host]) }

}

/** Transient visible answer text, kept separate from sourced workspace facts. */
internal fun LazyListScope.taskAnswerPanel(
    state: TaskViewUiState,
    onIntent: (TaskViewIntent) -> Unit,
    onReportAnswer: ((TaskAnswerProjection) -> Unit)? = null,
) {
    val answer = state.liveAnswer
    if (answer == null || !answer.hasSomethingToShow()) return
    item(key = "answer") {
        TaskAnswerBlock(
            answer = answer,
            modifier = Modifier.testTag(ANSWER_TEST_TAG),
            onReport = onReportAnswer?.let { report -> { report(answer) } },
        ) {
            TaskAnswerReadAloudControls(state = state, onIntent = onIntent)
        }
    }
}

/** The output panel: every fact, labelled by kind, with its source chip. */
internal fun LazyListScope.taskOutputPanel(
    facts: List<Fact>,
    sources: List<SourceRecord>,
    marks: Map<String, Bitmap> = emptyMap(),
) {
    val sourceById = sources.associateBy { it.id }
    item(key = "output-heading") {
        TaffySectionHeader(
            title = taffyString(R.string.taffy_task_view_output_title),
            modifier = if (facts.isNotEmpty()) Modifier.testTag(OUTPUT_TEST_TAG) else Modifier,
        )
    }
    if (facts.isEmpty()) {
        item(key = "output-empty") {
            TaffyEmptyState(
                title = taffyString(R.string.taffy_task_view_output_empty_title),
                body = taffyString(R.string.taffy_task_view_output_empty_body),
            )
        }
        return
    }
    itemsIndexed(
        items = facts,
        key = { index, fact -> "fact-$index-${fact.id.value}" },
    ) { _, fact -> TaskFactRow(fact, sourceById, marks) }

}

/** The tags the task view's list panels carry. */
const val TIMELINE_TEST_TAG: String = "task_view_timeline"
const val TIMELINE_NOTICE_TEST_TAG: String = "task_view_timeline_notice"
const val TIMELINE_ENTRY_TEST_TAG_PREFIX: String = "task_view_timeline_"
const val SOURCES_TEST_TAG: String = "task_view_sources"
const val ANSWER_TEST_TAG: String = "task_view_answer"
const val SOURCE_TEST_TAG_PREFIX: String = "task_view_source_"
const val OUTPUT_TEST_TAG: String = "task_view_output"
const val FACT_TEST_TAG_PREFIX: String = "task_view_fact_"

const val TIMELINE_TOGGLE_TEST_TAG: String = "task_view_timeline_toggle"

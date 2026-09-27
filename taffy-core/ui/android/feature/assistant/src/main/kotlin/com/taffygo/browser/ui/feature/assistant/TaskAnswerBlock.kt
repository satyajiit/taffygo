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
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.task.TaskAnswerProjection
import com.taffygo.browser.ui.core.ui.taffyString

/**
 * One visible answer, as the person reads it: its text, and a note when the
 * text is still arriving, stopped short, or was cut at the residency ceiling.
 *
 * Drawn wherever an answer is shown — under a question in the Ask overlay's
 * conversation, and in screen SCR-303's answer panel — so the two cannot
 * disagree about what a streaming or an incomplete answer looks like. The
 * segments are the accumulator's; the shared Markdown renderer coalesces
 * streaming updates and parses the bounded answer off the main thread.
 *
 * @param onReport opens the report sheet about this answer (decision 0253).
 *   Every surface that shows an answer passes one; the button is drawn once
 *   the answer has stopped arriving and has text to report.
 * @param trailing what follows the notes: the read-aloud controls, on the
 *   screen that has them.
 */
@Composable
internal fun TaskAnswerBlock(
    answer: TaskAnswerProjection,
    modifier: Modifier = Modifier,
    onReport: (() -> Unit)? = null,
    trailing: @Composable () -> Unit = {},
) {
    Column(
        verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.tight),
        modifier = modifier,
    ) {
        if (answer.segments.isNotEmpty()) {
            TaskAnswerText(answer.segments, answer.isStreaming)
        }
        if (answer.isStreaming) {
            Text(
                text = taffyString(R.string.taffy_task_view_answer_streaming),
                style = TaffyTheme.typography.detail,
                color = TaffyTheme.colors.textSecondary,
            )
        }
        if (answer.isIncomplete) {
            Text(
                text = taffyString(R.string.taffy_task_view_answer_incomplete),
                style = TaffyTheme.typography.detail,
                color = TaffyTheme.colors.caution,
            )
        }
        if (answer.isTruncated) {
            Text(
                text = taffyString(R.string.taffy_task_view_answer_truncated),
                style = TaffyTheme.typography.detail,
                color = TaffyTheme.colors.caution,
            )
        }
        trailing()
        if (onReport != null && answer.canBeReported()) {
            ReportAnswerButton(onClick = onReport)
        }
    }
}

/** Whether a person has finished reading text they could report. */
internal fun TaskAnswerProjection.canBeReported(): Boolean =
    !isStreaming && segments.any(String::isNotBlank)

/**
 * Whether there is anything to draw: text, or a note about the text there is
 * not. A streaming answer with nothing yet is not drawn as an empty block.
 */
internal fun TaskAnswerProjection.hasSomethingToShow(): Boolean =
    segments.isNotEmpty() || isIncomplete || isTruncated

// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.assistant

import androidx.annotation.StringRes
import androidx.compose.runtime.Composable
import androidx.compose.runtime.ReadOnlyComposable
import com.taffygo.browser.ui.core.model.TaskTimelineEntry
import com.taffygo.browser.ui.core.model.TaskTimelineKind
import com.taffygo.browser.ui.core.ui.taffyPlural
import com.taffygo.browser.ui.core.ui.taffyString

/**
 * The task view's words.
 *
 * Every one is a local template with a host and a count filled in. No branch
 * here can render page text or model text, because none of these functions
 * takes any.
 */
@Composable
@ReadOnlyComposable
internal fun timelineLine(entry: TaskTimelineEntry): String {
    val host = entry.host
    return when (entry.kind) {
        // Every page kind has a second sentence for the step that knows no
        // host. That is not a defensive branch: the core declines to name a
        // host it was not consented to rather than guessing one, so a step
        // with no host is an ordinary step and "Opened" with nothing after it
        // would be the defect.
        TaskTimelineKind.OPENED_PAGE -> when (host) {
            null -> taffyString(R.string.taffy_timeline_opened_page_unnamed)
            else -> taffyString(R.string.taffy_timeline_opened_page, host)
        }
        // Three sentences, because the count is joined from the workspace and
        // an errand's workspace has no facts in it. "Read croma.com — found 0
        // prices" is a number standing in for a fact that was never claimed.
        TaskTimelineKind.READ_PAGE -> when {
            host == null -> taffyString(R.string.taffy_timeline_read_page_unnamed)
            entry.count == 0 -> taffyString(R.string.taffy_timeline_read_page_plain, host)
            else -> taffyPlural(
                R.plurals.taffy_timeline_read_page,
                entry.count,
                host,
                entry.count,
            )
        }
        TaskTimelineKind.PAGE_UNAVAILABLE -> when (host) {
            null -> taffyString(R.string.taffy_timeline_page_unavailable_unnamed)
            else -> taffyString(R.string.taffy_timeline_page_unavailable, host)
        }
        TaskTimelineKind.MOVE_REFUSED -> when (host) {
            null -> taffyString(R.string.taffy_timeline_move_refused_unnamed)
            else -> taffyString(R.string.taffy_timeline_move_refused, host)
        }
        TaskTimelineKind.ASKED_YOU -> when (host) {
            null -> taffyString(R.string.taffy_timeline_asked_you_unnamed)
            else -> taffyString(R.string.taffy_timeline_asked_you, host)
        }
        TaskTimelineKind.YOU_ANSWERED -> taffyString(R.string.taffy_timeline_you_answered)
        TaskTimelineKind.HANDED_BACK -> when (host) {
            null -> taffyString(R.string.taffy_timeline_handed_back_unnamed)
            else -> taffyString(R.string.taffy_timeline_handed_back, host)
        }
        TaskTimelineKind.YOU_TOOK_OVER -> taffyString(R.string.taffy_timeline_you_took_over)
        TaskTimelineKind.BUILT_OUTPUT ->
            taffyPlural(R.plurals.taffy_timeline_built_output, entry.count, entry.count)
    }
}

/**
 * The heading of the notice the timeline carries when a task will not move.
 *
 * Three resources rather than one, because the same fact has to be said in
 * three places at three lengths: a heading where the steps would be, the
 * sentences under it, and one short line beside the state chip. Each is a
 * complete statement on its own — a person who reads only the line beside the
 * chip is not left with half of it.
 */
@StringRes
internal fun noticeTitle(notice: TaskNotice): Int = when (notice) {
    TaskNotice.CORE_UNAVAILABLE -> R.string.taffy_task_view_core_unavailable_title
    TaskNotice.RETRY_REQUIRED -> R.string.taffy_task_view_retry_required_title
}

/** Why nothing will happen, and what is true instead. */
@StringRes
internal fun noticeBody(notice: TaskNotice): Int = when (notice) {
    TaskNotice.CORE_UNAVAILABLE -> R.string.taffy_task_view_core_unavailable_body
    TaskNotice.RETRY_REQUIRED -> R.string.taffy_task_view_retry_required_body
}

/** The one line that goes beside the state, so the state is never read alone. */
@StringRes
internal fun noticeChipLine(notice: TaskNotice): Int = when (notice) {
    TaskNotice.CORE_UNAVAILABLE -> R.string.taffy_task_view_core_unavailable_chip
    TaskNotice.RETRY_REQUIRED -> R.string.taffy_task_view_retry_required_chip
}

// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.assistant

import com.taffygo.browser.ui.core.task.TaskAnswerProjection

/** Everything the report sheet can be asked to do. */
sealed interface ReportAnswerIntent {
    /** Open the sheet about [answer], the text the person just read. */
    data class Open(val answer: TaskAnswerProjection) : ReportAnswerIntent

    /**
     * The sheet handed an email draft to the phone. [opened] is false when no
     * app took it. Whether the email is then sent is the person's business.
     */
    data class EmailDraft(val opened: Boolean) : ReportAnswerIntent

    /**
     * Open the public repository's bug form in a new tab with [title] filled
     * in. The title is all an issue gets: an issue is public, and the answer
     * may carry what the person asked about.
     */
    data class OpenPublicIssue(val title: String) : ReportAnswerIntent
    data object Dismiss : ReportAnswerIntent
}

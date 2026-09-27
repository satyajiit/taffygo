// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.assistant

import com.taffygo.browser.ui.core.model.TaskControl

/** Everything screen SCR-301 can be asked to do. */
sealed interface AssistantBarIntent {

    /** Use one of the three controls. */
    data class Control(val control: TaskControl) : AssistantBarIntent

    /** Open task setup when idle, or the full task view when work exists. */
    data object OpenAssistant : AssistantBarIntent

    /** Open the pending approval. */
    data object ReviewApproval : AssistantBarIntent

    /** Open the results of a finished task. */
    data object OpenResults : AssistantBarIntent

    /** The person has done what Taffy asked on the page and hands it back. */
    data object CompleteHandover : AssistantBarIntent

    /** The person's answer to the question Taffy is waiting on. */
    data class Answer(val answer: String) : AssistantBarIntent
}

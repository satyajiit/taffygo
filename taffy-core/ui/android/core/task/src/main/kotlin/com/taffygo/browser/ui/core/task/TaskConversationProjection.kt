// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.task

/**
 * One task's conversation as this process has seen it: what the person asked,
 * and what Taffy answered under each question, oldest first.
 *
 * UI residency and never durable state. The questions are the ones the surfaces
 * sent from this process — the goal the task was started with, and each
 * follow-up after it — and the answers are the visible text the core streamed
 * back, paired to the question they came after. A restart forgets the lot,
 * which is the truth of it: the core keeps a task's transcript for its own
 * generation and no longer, and a conversation this layer could reconstruct
 * from nothing would be a conversation it made up.
 */
data class TaskConversationProjection(
    /** Every exchange, in the order the person asked. Never empty. */
    val exchanges: List<TaskExchange>,
) {
    /** The answer to the newest question, or null while nothing has come back for it. */
    val latestAnswer: TaskAnswerProjection?
        get() = exchanges.lastOrNull()?.answer
}

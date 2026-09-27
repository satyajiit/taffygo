// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.api

/**
 * One ordered event from a task's transient model-answer stream.
 *
 * The isolated core has already decoded the provider vocabulary and removed
 * reasoning, provider identities, and framing before this value exists. Text
 * is present on a delta and absent on the single terminal event. Nothing here
 * is snapshot or journal state; losing the process loses the partial answer.
 */
data class TaskAnswerReport(
    /** The task whose model call produced this event. */
    val taskId: String,
    /** The reducer-minted identity of that call. */
    val callId: String,
    /** Zero-based event sequence within [callId]. */
    val sequence: UInt,
    /** Sanitized visible text, absent only when [terminal] is true. */
    val text: String?,
    /** Whether this event closes [callId]. */
    val terminal: Boolean,
    /** Whether a terminal event represents one complete typed provider reply. */
    val complete: Boolean,
)

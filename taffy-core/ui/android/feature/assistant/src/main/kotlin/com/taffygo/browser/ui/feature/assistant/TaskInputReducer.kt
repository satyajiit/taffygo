// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.assistant

import com.taffygo.browser.ui.core.model.TaskInputRequest

/**
 * The pure half of the form sheet: what a described request looks like on
 * screen, and what an edit does to it.
 *
 * Both are functions a host test calls directly, which is what keeps the two
 * rules that matter testable without a device: dismissing discards nothing, and
 * a new description never overwrites what a person is part-way through typing.
 */
internal fun projectTaskInput(
    request: TaskInputRequest?,
    available: Boolean,
    previous: TaskInputUiState = TaskInputUiState(),
): TaskInputUiState {
    // Two closed answers folded into one. No request is nothing to draw; no
    // vault behind the seam is nothing that could be spent, so a form drawn
    // over it would be collecting a value with nowhere to put it.
    if (request == null || !available) return TaskInputUiState()
    val sameRequest = previous.requestId == request.requestId
    return TaskInputUiState(
        // A form the person has not seen opens itself. One they put away stays
        // away until they ask for it back — the whole of the dismissal rule,
        // and the reason this function takes the previous state at all.
        open = if (sameRequest) previous.open else true,
        requestId = request.requestId,
        host = request.host,
        approvalLifetimeSeconds = request.approvalLifetimeSeconds,
        rows = request.typedFields.map { field ->
            TaskInputUiState.Row(
                id = field.id,
                label = field.label,
                placeholder = field.placeholder,
                sensitive = field.sensitive,
                challenge = field.challenge,
                challengeImage = field.challengeImage,
                // Carried across a redescription of the same request, and
                // never across a different one. A browser that re-describes a
                // form — a picture refreshed, a row relabelled — must not empty
                // the field a person is half way through.
                value = if (sameRequest) previous.valueOf(field.id) else "",
            )
        },
        interactive = request.isInteractiveChallenge,
        // A fresh browser description can change a label or target while
        // retaining the request identity. Typed values survive; confirmation
        // never does, so the exact review must be seen again.
        reviewing = false,
        submitting = sameRequest && previous.submitting,
        failure = if (sameRequest) previous.failure else null,
    )
}

/** What editing, dismissing and reopening do. Sending is not a state change. */
internal fun reduceTaskInput(
    state: TaskInputUiState,
    intent: TaskInputIntent,
): TaskInputUiState = when (intent) {
    is TaskInputIntent.ValueChanged -> state.copy(
        rows = state.rows.map { row ->
            if (row.id == intent.fieldId) row.copy(value = intent.value) else row
        },
        reviewing = false,
        failure = null,
    )
    // Everything the person typed is still in `rows`, and `requestId` still
    // names the request the browser is still holding. Nothing about the task
    // changes: it stays waiting, which is what the pill then says.
    TaskInputIntent.Dismiss -> state.copy(open = false)
    TaskInputIntent.Reopen -> state.copy(open = true)
    TaskInputIntent.Submit -> if (state.canReview) state.copy(reviewing = true) else state
    TaskInputIntent.Edit -> state.copy(reviewing = false, failure = null)
    TaskInputIntent.Confirm,
    TaskInputIntent.CompleteInteractive,
    -> state
}

/** What has been typed into one row, or empty when the row is new. */
private fun TaskInputUiState.valueOf(fieldId: String): String =
    rows.firstOrNull { it.id == fieldId }?.value.orEmpty()

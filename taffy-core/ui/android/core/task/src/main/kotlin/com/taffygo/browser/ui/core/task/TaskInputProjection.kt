// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.task

import com.taffygo.browser.ui.core.api.TaskInputClient
import com.taffygo.browser.ui.core.model.TaskChallengeKind
import com.taffygo.browser.ui.core.model.TaskInputField
import com.taffygo.browser.ui.core.model.TaskInputRequest

/**
 * The closed classification of a described form, and the only place it happens.
 *
 * Public because it is the rule rather than an implementation detail: a host
 * test calls it directly, which is the whole point of keeping the decision out
 * of a view model.
 *
 * **One row this product cannot name refuses the whole request.** Dropping the
 * row would draw a form a person can complete and a site will reject, and
 * nothing on the screen could then say which part had never been asked for.
 * Refusing leaves one honest sentence to say instead — and leaves the browser
 * holding the request, so the task stays waiting rather than being answered
 * with a form that was never shown.
 */
fun taskInputRequestFrom(described: TaskInputClient.Described?): TaskInputRequest? {
    if (described == null) return null
    val classified = ArrayList<TaskInputField>(described.fields.size)
    for (field in described.fields) {
        val kind = TaskChallengeKind.fromWire(field.challenge) ?: return null
        classified += TaskInputField(
            id = field.id,
            label = field.label,
            placeholder = field.placeholder,
            sensitive = field.sensitive,
            challenge = kind,
            challengeImage = field.challengeImage,
            // Absent rather than a rectangle of zeroes, so a surface asking
            // "where is it" gets "nowhere was described" instead of a
            // degenerate box in the top-left corner of the page.
            highlight = if (field.hasHighlight) {
                TaskInputField.Highlight(
                    left = field.highlightLeft,
                    top = field.highlightTop,
                    right = field.highlightRight,
                    bottom = field.highlightBottom,
                )
            } else {
                null
            },
        )
    }
    return TaskInputRequest.of(
        described.requestId,
        described.host,
        classified,
        described.approvalLifetimeSeconds,
    )
}

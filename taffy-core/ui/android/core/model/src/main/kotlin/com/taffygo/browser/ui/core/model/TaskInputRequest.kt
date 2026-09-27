// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.model

/**
 * A form Taffy is holding open, and the whole of what a surface may draw for
 * it.
 *
 * The request is the unit, not the field. [of] refuses the whole request rather
 * than accepting a shortened one, and the seam that classifies a described row
 * into a [TaskInputField] refuses the same way on a kind it cannot name: a form
 * drawn with a row missing is a form a person completes and a site rejects,
 * with nothing on the screen able to say which part was never asked for.
 * Failing the request closed leaves one honest sentence to say instead.
 *
 * Nothing here is a value the person typed. This is the description of what to
 * ask for; the answers live in the sheet's own memory and go to the browser's
 * vault (decision
 * `docs/decisions/0063-a-field-value-is-minted-by-the-browser-and-spent-once.md`).
 */
data class TaskInputRequest(
    /** The opaque identity the browser minted for this request. */
    val requestId: String,
    /** The site the form belongs to, for the sentence that names it. */
    val host: String,
    /** Every row to draw, in the order the browser described them. */
    val fields: List<TaskInputField>,
    /** Exact browser-enforced lifetime beginning at confirmation. */
    val approvalLifetimeSeconds: Int = 120,
) {
    /**
     * Whether this request collapses to an instruction rather than a form.
     *
     * A widget in a cross-origin frame cannot be drawn in a sheet at all, so a
     * request that names one is a different surface: a card, a highlight over
     * the page, and a way to say when it is done.
     */
    val isInteractiveChallenge: Boolean
        get() = fields.any { it.challenge == TaskChallengeKind.INTERACTIVE_CHALLENGE }

    /** Where the interactive widget is on the page, when one was described. */
    val interactiveHighlight: TaskInputField.Highlight?
        get() = fields
            .firstOrNull { it.challenge == TaskChallengeKind.INTERACTIVE_CHALLENGE }
            ?.highlight
            ?.takeIf { !it.isEmpty }

    /** The rows a person types into, which an interactive challenge has none of. */
    val typedFields: List<TaskInputField>
        get() = fields.filter { it.challenge != TaskChallengeKind.INTERACTIVE_CHALLENGE }

    companion object {
        /**
         * The request these rows make, or null when there is nothing honest to
         * draw.
         *
         * Three ways to answer null, and each is the closed answer to a
         * question a surface would otherwise guess at: a request with no
         * identity to spend against, a form belonging to no named site — the
         * sentence about where what is typed goes cannot be written without one
         * — and a form with no rows in it.
         */
        fun of(
            requestId: String,
            host: String,
            fields: List<TaskInputField>,
            approvalLifetimeSeconds: Int = 120,
        ): TaskInputRequest? {
            if (requestId.isBlank() || host.isBlank() || fields.isEmpty() ||
                approvalLifetimeSeconds !in 1..300
            ) return null
            return TaskInputRequest(requestId, host, fields, approvalLifetimeSeconds)
        }
    }
}

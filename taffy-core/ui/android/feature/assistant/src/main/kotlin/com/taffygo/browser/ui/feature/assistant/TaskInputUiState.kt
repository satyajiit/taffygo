// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.assistant

import com.taffygo.browser.ui.core.common.FailureReason
import com.taffygo.browser.ui.core.model.TaskChallengeKind

/**
 * The form Taffy is holding open, as one screen renders it.
 *
 * Every row comes from a description the browser sent. There is no list of
 * field names anywhere in this feature, because the next site has different
 * ones and a compiled-in list is a product that works on one site.
 *
 * **What a person has typed is in [Row.value] and nowhere else.** It is held by
 * this state, which is held by one view model, which takes no
 * `SavedStateHandle` and records no analytics. [toString] on this type and on
 * [Row] is overridden for the same reason: a state object reaches a log the
 * moment anybody debugs a recomposition, and a generated `toString` would put a
 * one-time code in it.
 */
data class TaskInputUiState(
    /** Whether the sheet is on screen. False while a request is open is dismissal. */
    val open: Boolean = false,
    /** The opaque identity to spend the values against, or empty for none. */
    val requestId: String = "",
    /** The site the form belongs to; the data sentence names it. */
    val host: String = "",
    /** One row per described field, in the order they were described. */
    val rows: List<Row> = emptyList(),
    /** Browser-enforced one-use approval lifetime, beginning at confirmation. */
    val approvalLifetimeSeconds: Int = 0,
    /** Whether the exact values are currently visible for confirmation. */
    val reviewing: Boolean = false,
    /**
     * Whether this request is a widget on the page rather than a form.
     *
     * A different surface, not a variant of the same one: there is nothing to
     * type, so the sheet collapses to an instruction and a way to say when the
     * person is finished.
     */
    val interactive: Boolean = false,
    /** Whether one submission is in flight. */
    val submitting: Boolean = false,
    /** The last closed refusal, kept until the next attempt or edit. */
    val failure: FailureReason? = null,
) {
    /** Whether there is a request at all — open or dismissed. */
    val hasRequest: Boolean
        get() = requestId.isNotEmpty()

    /**
     * Whether the person has answered enough to send.
     *
     * Every described row, because the browser described the ones the site
     * needs and this layer is in no position to decide that one of them is
     * optional.
     */
    val canReview: Boolean
        get() = hasRequest &&
            !interactive &&
            !reviewing &&
            !submitting &&
            rows.isNotEmpty() &&
            rows.all { it.value.isNotBlank() }

    /** Whether the visible exact-value approval can be spent now. */
    val canConfirm: Boolean
        get() = hasRequest && reviewing && !interactive && !submitting &&
            approvalLifetimeSeconds > 0 && rows.isNotEmpty() &&
            rows.all { it.value.isNotBlank() }

    /** Whether the interactive card's one control is live. */
    val canCompleteInteractive: Boolean
        get() = hasRequest && interactive && !submitting

    override fun toString(): String =
        "TaskInputUiState(open=$open, requestId=$requestId, host=$host, " +
            "rows=${rows.size}, reviewing=$reviewing, interactive=$interactive, " +
            "submitting=$submitting, " +
            "failure=$failure)"

    /** One row of the form, and what has been typed into it so far. */
    data class Row(
        /** The browser's own identity for the field. Never shown to a person. */
        val id: String,
        /** What to call it, as the page called it. */
        val label: String,
        /** A hint under the field, when the page offered one. */
        val placeholder: String? = null,
        /** Whether what is typed here must not be legible on screen. */
        val sensitive: Boolean = false,
        /** What kind of field it is. */
        val challenge: TaskChallengeKind = TaskChallengeKind.NONE,
        /** The picture to read back, as bytes the browser already fetched. */
        val challengeImage: ByteArray? = null,
        /** What the person has typed. Never leaves this process except to the vault. */
        val value: String = "",
    ) {
        override fun equals(other: Any?): Boolean {
            if (this === other) return true
            if (other !is Row) return false
            return id == other.id &&
                label == other.label &&
                placeholder == other.placeholder &&
                sensitive == other.sensitive &&
                challenge == other.challenge &&
                challengeImage.contentEquals(other.challengeImage) &&
                value == other.value
        }

        override fun hashCode(): Int {
            var result = id.hashCode()
            result = 31 * result + label.hashCode()
            result = 31 * result + placeholder.hashCode()
            result = 31 * result + sensitive.hashCode()
            result = 31 * result + challenge.hashCode()
            result = 31 * result + challengeImage.contentHashCode()
            result = 31 * result + value.hashCode()
            return result
        }

        /** Named, never quoted: the whole point of this type is what it holds. */
        override fun toString(): String =
            "Row(id=$id, challenge=$challenge, typed=${value.isNotEmpty()})"
    }
}

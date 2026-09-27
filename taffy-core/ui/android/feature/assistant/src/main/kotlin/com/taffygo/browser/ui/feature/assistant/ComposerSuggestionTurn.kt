// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.assistant

import com.taffygo.browser.ui.core.api.ComposerCompletionReport

/**
 * Which suggestion request the composer is on, and what an arriving answer does
 * to it (decision 0097 section 3).
 *
 * Pure, so the rule that matters most here can be proved without a clock, a
 * core or a screen: exactly one request is current, a newer identity supersedes
 * an older one, and an answer whose identity the composer has moved past is
 * discarded on arrival rather than shown.
 *
 * Superseding is done here, by moving on and refusing what the old request says
 * when it lands — the browser stops a displaced dispatch of its own accord, so
 * a newer identity is the whole of it. Withdrawing is the other case and is not
 * this type's: a composer that stopped wanting an answer without asking for
 * another says so on the seam, and [forgotten] is where it learns which request
 * that was.
 */
data class ComposerSuggestionTurn(
    /** The request this composer is on, null when it is on none. */
    val awaiting: String? = null,
    /** What is offered right now. */
    val offered: ComposerSuggestion = ComposerSuggestion.None,
) {
    /**
     * Move to [requestId].
     *
     * Whatever was offered goes with it. A person who has typed another word
     * has moved past the suggestion they were shown, and leaving it standing
     * while a newer one is asked for would offer text for a sentence that no
     * longer exists.
     */
    fun asked(requestId: String): ComposerSuggestionTurn =
        ComposerSuggestionTurn(awaiting = requestId, offered = ComposerSuggestion.None)

    /**
     * Fold in one answer.
     *
     * An answer to anything but [awaiting] is discarded and this is unchanged —
     * including an answer that arrives after the composer has stopped waiting
     * altogether, which is the same fact.
     */
    fun answered(report: ComposerCompletionReport): ComposerSuggestionTurn =
        if (report.requestId != awaiting) {
            this
        } else {
            copy(awaiting = null, offered = ComposerSuggestion.of(report.text))
        }

    /** Stop waiting and offer nothing: the field emptied, or suggestions went off. */
    fun forgotten(): ComposerSuggestionTurn = ComposerSuggestionTurn()

    /** Whether an answer is still owed. */
    val waiting: Boolean get() = awaiting != null
}

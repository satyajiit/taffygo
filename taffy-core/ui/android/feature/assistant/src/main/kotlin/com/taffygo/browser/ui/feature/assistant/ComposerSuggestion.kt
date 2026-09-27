// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.assistant

/**
 * What one composer suggestion request answered (decision
 * `docs/decisions/0097-a-composer-suggestion-is-spent-from-the-persons-own-key.md`).
 *
 * Three answers rather than a nullable string, because the seam carries three
 * facts and two of them look alike. `ComposerCompletionReport.text` is null
 * when the request produced no suggestion and empty when it produced a
 * suggestion of no characters; a surface that folded those together would draw
 * an empty ghost over the cursor for the first. Keeping the difference in the
 * type rather than in a convention is what stops every caller having to
 * remember it.
 *
 * None of the three is a failure. A request that answers with nothing is a
 * request that worked and had nothing to offer.
 */
sealed interface ComposerSuggestion {

    /** Nothing is offered: nothing was asked, or the answer carried no text. */
    data object None : ComposerSuggestion

    /**
     * The answer carried a suggestion of no characters.
     *
     * Nothing is drawn and there is nothing to accept, exactly as for [None] —
     * but it arrived by a different road, and the two are told apart here so
     * that neither can be read as the other.
     */
    data object NoCharacters : ComposerSuggestion

    /** Characters to offer after what the person has typed. */
    data class Ghost(val text: String) : ComposerSuggestion

    companion object {
        /** The report's absent-or-present text, read as one of the three. */
        fun of(text: String?): ComposerSuggestion = when {
            text == null -> None
            text.isEmpty() -> NoCharacters
            else -> Ghost(text)
        }
    }
}

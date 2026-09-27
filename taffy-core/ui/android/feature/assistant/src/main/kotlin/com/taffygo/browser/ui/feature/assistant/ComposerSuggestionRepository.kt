// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.assistant

import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.flowOf

/**
 * The composer's suggestion lifecycle (decision 0097).
 *
 * Cold on purpose. Nothing is asked for until a surface collects, and
 * everything stops the moment it stops collecting — which is how "off until the
 * person chooses it" is enforced by construction rather than by a flag every
 * caller has to remember to read: a composer whose setting is off never
 * collects, so no request is ever made.
 *
 * The debounce and the superseding live here rather than on the seam, because
 * the surface is the only place that knows a person is still typing.
 */
interface ComposerSuggestionRepository {

    /**
     * Ghost text for what is in the composer, one live request at a time.
     *
     * [typed] carries both halves of the composer's text — what stands before
     * the caret and what stands after it — and null when the composer has no
     * single insertion point to complete at, which is what a range selection
     * is. Null is a value here rather than an absent emission on purpose: the
     * composer moving from a caret to a selection is something this lifecycle
     * has to see, so that a request already in flight is superseded rather than
     * left to arrive and be drawn over words the person is about to replace.
     */
    fun suggestions(typed: Flow<ComposerCaretText?>): Flow<ComposerSuggestion>

    /** No suggestions on this build. Nothing is asked and nothing is offered. */
    class Absent : ComposerSuggestionRepository {
        override fun suggestions(typed: Flow<ComposerCaretText?>): Flow<ComposerSuggestion> =
            flowOf(ComposerSuggestion.None)
    }
}

/** How long typing has to stop before a suggestion is worth a model call. */
internal const val COMPOSER_SUGGESTION_DEBOUNCE_MILLIS: Long = 350L

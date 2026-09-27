// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.providers

import com.taffygo.browser.ui.core.model.ProviderRosterState

/**
 * The half of screen SCR-417 that changes nothing outside the screen.
 *
 * Three of the page's intents move only what the person can see: typing,
 * disclosing the providers that still need setting up, and — for the two that
 * do reach the core — recording what was asked so the row can say a command is
 * out. They are here so a host test can drive the whole of the page's local
 * behaviour without a repository, a device or a provider.
 *
 * The command itself is absent on purpose, as it is on screen SCR-415. A
 * reducer that also "chose" would have to model a call it cannot make, and the
 * first thing to go wrong would be a page reporting a model pinned that the
 * core never accepted.
 */
object ModelSelectionReducer {

    /**
     * Fold one intent into the draft.
     *
     * A press on a locked row records nothing. Its provider has no credential
     * behind it, so the command would state a choice the browser cannot act
     * on; the press navigates instead, which is [ModelSelectionIntent.OpenProvider]'s
     * whole job.
     */
    fun reduce(draft: ModelSelectionDraft, intent: ModelSelectionIntent): ModelSelectionDraft =
        when (intent) {
            is ModelSelectionIntent.Search -> draft.copy(query = intent.query)

            ModelSelectionIntent.ToggleLocked ->
                draft.copy(lockedExpanded = !draft.lockedExpanded)

            is ModelSelectionIntent.ChooseModel ->
                if (intent.row.locked) {
                    draft
                } else {
                    draft.copy(
                        asked = ModelSelectionAsk(
                            providerId = intent.row.providerId,
                            modelId = intent.row.modelId,
                            thinking = intent.row.thinkingAfterChoosing,
                        ),
                    )
                }

            is ModelSelectionIntent.ChooseThinking ->
                if (intent.row.locked) {
                    draft
                } else {
                    draft.copy(
                        asked = ModelSelectionAsk(
                            providerId = intent.row.providerId,
                            modelId = intent.row.modelId,
                            thinking = intent.level,
                        ),
                    )
                }

            is ModelSelectionIntent.OpenProvider -> draft
        }

    /**
     * Drop an ask the published roster has already answered.
     *
     * This is what makes the echo the only thing the page believes. The core
     * publishes the accepted choice as the next roster, and the moment that
     * roster states both halves of what was asked, the ask has been answered
     * and stops being drawn as outstanding — the row it named goes on being
     * marked in use by the roster rather than by the press.
     *
     * A provider the roster no longer carries settles too, and for a stronger
     * reason: nothing is left that could ever answer.
     *
     * It is applied where the draft is *read* rather than where it is stored.
     * A pure fold has nowhere to write back to, and an ask that is never
     * rendered is not a state — the next press replaces it whole.
     */
    fun settle(draft: ModelSelectionDraft, roster: ProviderRosterState): ModelSelectionDraft {
        val ask = draft.asked ?: return draft
        val row = roster.rows.firstOrNull { it.providerId == ask.providerId }
            ?: return draft.copy(asked = null)
        val answered = row.selectedModelId == ask.modelId && row.thinking == ask.thinking
        return if (answered) draft.copy(asked = null) else draft
    }

    /**
     * Drop an ask whose command never left.
     *
     * The core refused it or could not be reached, so no roster will ever
     * answer it and leaving the row saying so would be a wait with nothing at
     * the end of it.
     */
    fun abandon(draft: ModelSelectionDraft): ModelSelectionDraft = draft.copy(asked = null)
}

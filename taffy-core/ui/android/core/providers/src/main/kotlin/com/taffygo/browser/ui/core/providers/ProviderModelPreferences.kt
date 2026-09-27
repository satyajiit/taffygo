// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.providers

import com.taffygo.browser.ui.core.model.ThinkingLevel

/**
 * One person's standing choice for one provider, stated whole (decision
 * `docs/decisions/0093-a-model-choice-is-one-standing-state.md`).
 *
 * Its own seam rather than a method on [ProviderRosterRepository], which stays
 * read-only for the reason its own documentation gives: the roster is what the
 * core published, and this is what a person asked for. Nothing here reports
 * what happened either — the accepted choice comes back on the next published
 * roster, so a surface renders that echo and never what it sent.
 */
interface ProviderModelPreferences {

    /**
     * State the whole choice for [providerId].
     *
     * Both [modelId] and [thinking] are the choice as it should now stand
     * rather than a change to apply, so passing null for either asks for the
     * default that null means: the provider's own model order, and Taffy
     * deciding how much thinking to ask for. They travel together because they
     * are one state — a model write followed by a thinking write is two states
     * a process death can land between, and each of them silently clears what
     * the other did not name.
     */
    suspend fun choose(providerId: String, modelId: String?, thinking: ThinkingLevel?)

    /** No provider preference can be stated on this build. */
    class Unavailable : ProviderModelPreferences {
        override suspend fun choose(
            providerId: String,
            modelId: String?,
            thinking: ThinkingLevel?,
        ) = Unit
    }
}

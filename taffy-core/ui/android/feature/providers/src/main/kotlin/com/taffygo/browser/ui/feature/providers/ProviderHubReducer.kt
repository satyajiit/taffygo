// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.providers

/**
 * The one thing screen SCR-404 folds instead of navigating.
 *
 * Every other control on the hub sends a person somewhere and the surface they
 * arrive at owns the change, which is why this screen has no draft and no
 * in-flight flag. Choosing a category is different: nothing is written and
 * nowhere is opened, so the answer has to be held here or the tabs would be a
 * row of controls that did nothing.
 *
 * It is a fold rather than a field the screen writes for itself so that the
 * rule survives a republication. `ProviderHubProjection.project` rebuilds the
 * whole state every time the core publishes a roster — a completed sign-in, a
 * saved key, a catalog refresh — and the chosen category is applied on top of
 * each fresh projection. Left to the projection alone, a person reading the
 * subscription tab would be thrown back to Connected the moment a credential
 * they saved on another screen was echoed back.
 */
object ProviderHubReducer {

    /**
     * Fold an intent into the screen.
     *
     * The two navigating intents return the state unchanged rather than being
     * left out: a `when` over the sealed set is what makes a new intent that
     * changes the screen impossible to add without deciding here whether it
     * does.
     */
    fun reduce(state: ProviderHubUiState, intent: ProviderHubIntent): ProviderHubUiState =
        when (intent) {
            is ProviderHubIntent.ShowCategory -> state.copy(showing = intent.group)
            is ProviderHubIntent.OpenRow, ProviderHubIntent.AddYourOwnProvider -> state
        }
}

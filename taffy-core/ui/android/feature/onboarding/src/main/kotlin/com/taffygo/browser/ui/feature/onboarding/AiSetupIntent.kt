// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.onboarding

import com.taffygo.browser.ui.core.model.ProviderRoute

/** Everything screen SCR-004 can be asked to do. */
sealed interface AiSetupIntent {

    /** Send model requests this way. */
    data class ChooseRoute(val route: ProviderRoute) : AiSetupIntent

    /**
     * Finish the sequence with the chosen route.
     *
     * Ignored when nothing is chosen, because the action that does not need a
     * choice is [SetUpLater] and the two must not be the same button.
     */
    data object Finish : AiSetupIntent

    /**
     * Finish the sequence without choosing, leaving the route
     * [ProviderRoute.NOT_CONFIGURED].
     *
     * This is a real answer, not a deferral the product then nags about: the
     * browser is complete without a model, and a person who never sets one up
     * has a working browser forever. What it costs them is the assistant, and
     * the readiness panels that already read `NotSetUp` say so where the
     * assistant would have been, each routing to AI and providers.
     */
    data object SetUpLater : AiSetupIntent
}

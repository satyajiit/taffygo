// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.onboarding

import com.taffygo.browser.ui.core.model.ProviderRoute

/**
 * Screen SCR-004 — AI setup.
 *
 * Choosing nothing is the starting state, and it is now a finishable one:
 * the route card is unselected, the primary action is disabled until it is
 * chosen, and "set up later" finishes the sequence with the route still
 * [ProviderRoute.NOT_CONFIGURED]. Browsing starts either way. Nothing on this
 * screen depends on an account, because there is none to depend on.
 */
data class AiSetupUiState(
    /** Where model requests go, as it stands. */
    val route: ProviderRoute = ProviderRoute.NOT_CONFIGURED,
    /** Whether the final choice is frozen while it is being saved. */
    val finishing: Boolean = false,
) {
    /** Whether a first-run route has been chosen. */
    val hasChosen: Boolean
        get() = route == ProviderRoute.DIRECT_WITH_YOUR_KEY
}

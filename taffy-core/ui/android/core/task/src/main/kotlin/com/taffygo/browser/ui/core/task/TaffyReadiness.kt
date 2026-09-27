// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.task

import com.taffygo.browser.ui.core.model.ProviderRoute

/**
 * Whether Taffy can reach a provider at all, read from facts.
 *
 * The saved route (screen SCR-004's choice, `UserPreferences.providerRoute`)
 * is what a consent carries; it is not what makes a request answerable. A key
 * that is stored and usable, or a provider address of the person's own, is.
 * This verdict is the one the Ask sheet, the start page's box and the pill
 * read, so that "nothing is set up" is said only when the AI & providers
 * screen (SCR-404) would list no connected provider either — the rule mirrors
 * that screen's own row availability.
 */
sealed interface TaffyReadiness {
    /**
     * The core has not published a complete projection yet. Surfaces draw as
     * they always did; nothing is refused and nothing is promised.
     */
    data object Unknown : TaffyReadiness

    /** No usable key, no own address, and no route chosen that names one. */
    data object NotSetUp : TaffyReadiness

    /**
     * A route is chosen — by the person, or implied by a key that is stored
     * but not ready — and nothing behind it can answer. The surface names the
     * route, because what is missing depends on it.
     */
    data class RouteChosenButNothingBehindIt(val route: ProviderRoute) : TaffyReadiness

    /** A request would reach a provider by [route]. */
    data class Ready(val route: ProviderRoute) : TaffyReadiness

    /** Whether a surface shows the set-up panel in place of its composer. */
    val needsSetup: Boolean
        get() = this is NotSetUp || this is RouteChosenButNothingBehindIt

    /** The route a request would take, or none: the value a disclosure sentence reads. */
    val routeOrNone: ProviderRoute
        get() = if (this is Ready) route else ProviderRoute.NOT_CONFIGURED
}

/**
 * The readiness rule. A pure function, so a host test can table it.
 *
 * One route names a way a request can go, and it is honoured on its own
 * terms: a person who chose it and holds no usable key is told the key is
 * missing rather than being answered from some other fact. A route that names
 * nothing (`NOT_CONFIGURED`, or `NO_MODEL_REQUIRED` from a shape that needs no
 * model) lets the facts decide: a usable key or own address first, then a key
 * that exists but is not ready, then nothing.
 */
fun taffyReadiness(facts: ProviderReadinessFacts, chosen: ProviderRoute): TaffyReadiness {
    if (!facts.known) return TaffyReadiness.Unknown
    val direct = facts.answeringProviderIds.isNotEmpty()
    val pending = (facts.storedCredentialProviderIds + facts.heldCredentialProviderIds)
        .minus(facts.usableCredentialProviderIds)
        .isNotEmpty()
    return when (chosen) {
        ProviderRoute.DIRECT_WITH_YOUR_KEY ->
            if (direct) {
                TaffyReadiness.Ready(ProviderRoute.DIRECT_WITH_YOUR_KEY)
            } else {
                TaffyReadiness.RouteChosenButNothingBehindIt(ProviderRoute.DIRECT_WITH_YOUR_KEY)
            }
        ProviderRoute.NOT_CONFIGURED, ProviderRoute.NO_MODEL_REQUIRED -> when {
            direct -> TaffyReadiness.Ready(ProviderRoute.DIRECT_WITH_YOUR_KEY)
            pending -> TaffyReadiness.RouteChosenButNothingBehindIt(ProviderRoute.DIRECT_WITH_YOUR_KEY)
            else -> TaffyReadiness.NotSetUp
        }
    }
}

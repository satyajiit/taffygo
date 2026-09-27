// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.browser

import kotlinx.coroutines.flow.StateFlow

/**
 * Opening, walking and closing the one page an errand is run on.
 *
 * ## Why this is its own seam rather than three more methods on [BrowserMediator]
 *
 * Everything on that interface is about the person's own tabs: what is open,
 * which is selected, what the switcher counts. An errand page is none of those
 * things. It is never published to a tab model, so it is never in the tab list,
 * never persisted, and never restored — and a method that opened one from the
 * tab mediator would be an invitation to treat it as a tab, which is the defect
 * this surface exists to fix.
 *
 * ## One at a time, and that is a rule rather than a limit
 *
 * [page] is a single value because a person is running one errand: they tapped
 * Sign in on one provider, or asked for one vendor's key page. A second [open]
 * while one is live closes the first, because the alternative is a page nothing
 * on screen can reach and nothing will ever close.
 *
 * ## Honesty
 *
 * [open] answers null when there is no engine behind this build, when the
 * address is not one this seam will open, or when the page could not be built.
 * A caller that gets null has not opened anything and must say so — the
 * provider sign-in broker reads exactly that answer and falls back to its
 * device-code shape rather than waiting out a ten-minute deadline for a page
 * that was never on screen.
 */
interface ErrandPagePort {

    /**
     * The errand now running, or null when none is.
     *
     * Null is also what a caller sees the moment [close] lands, which is what
     * lets screen SCR-110 tell "my page went away" from "my page is loading".
     */
    val page: StateFlow<ErrandPage?>

    /**
     * Opens [url] as an errand and answers its identity.
     *
     * The address must be `https`, with a host, no user information and no
     * fragment. Anything else answers null rather than being repaired: every
     * caller of this seam is handing over an address the product itself
     * resolved, so a malformed one is a defect upstream and not a person's
     * typing.
     */
    suspend fun open(url: String): String?

    /**
     * Walks this errand's own history back one page.
     *
     * Answers whether it moved. False means the page had nowhere to go, which
     * is the screen's signal to leave the errand instead.
     *
     * Not suspending, and neither is [close]: both are main-thread acts, and
     * both are called from places that have no coroutine to wait in — a click
     * handler and a composition being disposed. A suspending [close] in
     * particular would be unreachable at the moment it matters most, because a
     * screen's own scope is already cancelled by the time it is torn down.
     */
    fun goBack(errandId: String): Boolean

    /**
     * Ends the errand and destroys its page.
     *
     * Naming the identity rather than closing whatever is current, so a screen
     * that was already replaced cannot close somebody else's errand on its way
     * out.
     *
     * Called from the composition's own disposal, which is what makes an errand
     * end when its screen goes — including when something else replaced the
     * whole back stack, which is how an inbound link arriving mid-errand is
     * survived.
     */
    fun close(errandId: String)
}

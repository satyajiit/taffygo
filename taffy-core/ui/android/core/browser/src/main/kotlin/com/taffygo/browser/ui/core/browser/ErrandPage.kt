// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.browser

/**
 * One page the product opened for a single errand, as screen SCR-110 draws it.
 *
 * An errand is a page a person was *sent* to rather than one they browsed to: a
 * vendor's sign-in, the page a key is fetched from, a vendor's own
 * documentation. It is not a tab. It has no entry in the tab switcher, it is
 * not counted there, it is never written to the session on disk, and the only
 * way out of it is the toolbar above it.
 *
 * ## Why the whole navigation is carried rather than four fields
 *
 * A vendor's page is about to be handed a credential, and the one thing that
 * tells a person whose page is asking is the address. [NavigationState] is
 * where this product has already worked out how to say that safely — the host
 * alone, userinfo dropped so `https://apple.com@evil.test/` cannot read as
 * Apple, and a lock that is off whenever the certificate did not check out.
 * Screen SCR-110 draws the same pill screen SCR-101 does, from the same record,
 * so the two cannot come to disagree about what an origin looks like.
 *
 * What SCR-110 does *not* draw from it is anything to act on: no reload, no
 * stop, no forward, and no way to navigate by typing. The pill is read-only
 * there, which is the whole difference between this surface and the browsing
 * one.
 *
 * `navigation.canGoBack` is the page's own history, not the back stack. Back on
 * SCR-110 walks the vendor's pages first — an account chooser and the consent
 * screen after it are two pages — and leaves the errand only when there is
 * nothing left to walk.
 */
data class ErrandPage(
    /**
     * The identity minted when this page was opened.
     *
     * The route carries this and never a URL, the way the Ask sheet's route
     * carries an ask id and not a question: an address in a saved route is an
     * address that outlives the page it named.
     */
    val errandId: String,
    /** Where this errand is, as every other Taffy-owned surface sees a page. */
    val navigation: NavigationState,
)

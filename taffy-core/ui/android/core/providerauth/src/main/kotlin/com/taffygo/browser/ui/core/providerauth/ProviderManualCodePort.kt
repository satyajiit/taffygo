// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.providerauth

/**
 * The manual-code fallback of decision 0095 section 2, handed to the browser.
 *
 * A PKCE sign-in normally finishes when the browser intercepts the vendor's
 * redirect. When it does not come back — the vendor moved its redirect
 * address, or showed the code on a page instead — the person can paste what
 * the vendor displayed, or the address the tab landed on, on trusted chrome.
 * Either is handed over *whole*: the browser's provider auth broker owns the
 * shape (some vendors join the code and the state with a `#`), every bound
 * on it, and the claim that it belongs to a live flow. Android neither
 * splits nor inspects the value, and it is never logged.
 *
 * This is a port rather than a Core API command on purpose. The redirect
 * claim is the browser broker's fact (decision 0081): the core learns that
 * the grant arrived the same way it learns of an intercepted redirect, as an
 * `EXCHANGING` event, so there is nothing for the core to be told here.
 *
 * `false` means no live flow accepted the entry — the flow identity is not
 * current, the vendor's flow is not PKCE, or the value did not fit. A screen
 * shows that as "that code did not work" rather than as an error, because the
 * person can try again until the flow's deadline. The port must not throw for
 * a browser that merely declined.
 */
fun interface ProviderManualCodePort {
    /** Hand [entered] to the browser for the flow named [flowId]. */
    suspend fun submit(flowId: String, entered: String): Boolean
}

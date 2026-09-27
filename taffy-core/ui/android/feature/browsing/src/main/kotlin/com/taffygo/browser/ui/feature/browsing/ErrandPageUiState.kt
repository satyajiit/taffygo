// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

/**
 * What screen SCR-110 renders.
 *
 * The whole state of a page a person was *sent* to. There is no draft here, no
 * suggestion list and no forward: the toolbar has a way back, an origin to read
 * and a way out, and everything else about this page belongs to the vendor
 * whose page it is.
 */
data class ErrandPageUiState(
    /** The origin, drawn read-only. Empty until the first navigation commits. */
    val host: String = "",
    /** Whether the connection is private, drawn as the pill's lock. */
    val isSecure: Boolean = false,
    /** Whether a navigation is in flight, drawn as the pill's rail. */
    val isLoading: Boolean = false,
    /** Requests blocked on this page, for the pill's badge. */
    val blockedCount: Int = 0,
    /** Whether the vendor's own history has somewhere to go back to. */
    val canGoBack: Boolean = false,
    /**
     * Whether the page this route named is gone.
     *
     * True after process death — the route came back and the page did not — and
     * after anything else closed the page underneath this screen. The screen
     * leaves rather than drawing a page host with nothing in it, which is the
     * honest answer and the only one: the address is not in the route, and
     * there is deliberately nothing here to reopen it from.
     */
    val gone: Boolean = false,
)

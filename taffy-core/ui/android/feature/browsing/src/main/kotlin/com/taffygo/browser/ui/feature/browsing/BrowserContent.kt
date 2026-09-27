// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

/**
 * The one thing screen SCR-101's page area is drawing right now.
 *
 * An enumeration rather than the pair of booleans this used to be, because the
 * area has four answers and only one of them can be true at a time. The
 * precedence between them was written down twice — once in the screen's `when`
 * and once in a derived `showsContent` flag — and the two had to be read
 * together to know what would appear. They are one answer now, decided in
 * [BrowserMainUiState] where a host test can name it.
 *
 * **There is deliberately no `LOADING`.** A page arriving is not a fifth thing
 * the area draws; it is the same page, still there, while the next one is
 * fetched. `docs/design/ux-spec.md` §2 gives this area to the web and says it
 * is never covered by Taffy's UI, and a skeleton over the live surface broke
 * that rule to replace a readable page with a grey rectangle. Loading is drawn
 * on the chrome instead — see `TaffyAddressPill`.
 */
enum class BrowserContent {

    /** The page itself: the live surface, uncovered. */
    PAGE,

    /** The page did not load, and screen SCR-108's words say why. */
    FAILED,

    /**
     * The tab has not been anywhere, and the Python library is not installed.
     *
     * The start page is the product looking ready. It is not drawn until the
     * library page intelligence needs is on the device.
     */
    PREPARING,

    /**
     * The tab has not been anywhere, so TaffyGo's own start content is drawn.
     *
     * The state that had no name at all until this existed. A tab with no page
     * still has a page *host* — the engine's surface is composed, because
     * taking it out of the composition would destroy the compositor's output
     * surface on every navigation — and with nothing loaded into it that host
     * paints the blank document, which on a phone in the dark theme is a
     * white slab over two thirds of the display.
     */
    START,
}

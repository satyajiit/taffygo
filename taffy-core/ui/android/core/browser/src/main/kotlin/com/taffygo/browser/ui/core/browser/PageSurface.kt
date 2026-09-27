// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.browser

import android.content.Context
import android.view.View

/**
 * Where the web page's own pixels go.
 *
 * [BrowserMediator] is what a Taffy-owned surface may *ask* of the browser;
 * this is where the browser's answer is *drawn*. They are separate seams
 * because they have separate lifetimes: navigation state is a flow a view model
 * collects, while the page is a platform view a composition attaches and
 * releases.
 *
 * ## Why `android.view.View` and nothing narrower
 *
 * The page is rendered by Chromium into a `SurfaceView` it owns. This module
 * cannot name that type because it is a portable UI module, and it must not:
 * naming it would put a Chromium implementation type in a public UI port. `View` is the widest
 * type both builds already have, and it is the exact type the Compose
 * `AndroidView` interop wants back, so nothing is lost by stopping here.
 *
 * There is deliberately no Compose in this file. `:core:browser` is layer 1 and
 * publishes no UI; the composable that renders one of these lives in
 * `:core:ui`, which is layer 2 and may depend on this.
 *
 * ## Honesty
 *
 * [isLive] is the whole of the contract's honesty. A build with no web engine
 * behind it answers `false`, and the surfaces above draw their placeholder
 * rather than an empty frame that looks like a page which failed to paint. No
 * implementation may answer `true` and then hand back nothing from [attach].
 */
interface PageSurface {

    /**
     * Whether there is a real web engine behind this.
     *
     * `false` before native initialization has attached the browser page host.
     * A screen reads this to decide
     * whether it is showing a page at all.
     */
    val isLive: Boolean

    /**
     * The view that holds the page, ready to be added to a parent.
     *
     * Returns `null` when [isLive] is `false`, which is the only case a caller
     * has to handle: a live surface always has a view. The caller adds it to
     * exactly one parent and gives it back to [detach] when that parent goes
     * away.
     */
    fun attach(context: Context): View?

    /**
     * Give the view back.
     *
     * The surface, not the caller, decides what happens next — a page host is
     * usually kept alive across an attach/detach pair, because tearing down the
     * compositor's output surface is not free. Calling this with a view that
     * did not come from [attach] is a programming error.
     */
    fun detach(view: View)
}

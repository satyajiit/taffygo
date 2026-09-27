// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.ui

import androidx.compose.runtime.Composable
import androidx.compose.runtime.staticCompositionLocalOf
import androidx.compose.ui.Modifier
import androidx.compose.ui.viewinterop.AndroidView
import com.taffygo.browser.ui.core.browser.BrowserGraph
import com.taffygo.browser.ui.core.browser.PageSurface

/**
 * Draw the web page, the only way a Taffy-owned surface draws one.
 *
 * The page is a platform view, so this is an `AndroidView` and there is no way
 * around that: Chromium renders into a `SurfaceView` it owns, and a `SurfaceView`
 * is a window-level object rather than something Compose can paint. Everything
 * that follows from that is a constraint on the *caller*, and it is worth
 * stating here because none of it is visible at the call site:
 *
 * - **Nothing may transform an ancestor of this.** A `graphicsLayer`, an alpha,
 *   a clip or an animated offset applied above this composable moves the
 *   Compose layer and leaves the surface exactly where it was. The page would
 *   sit still while everything around it moved. That is why the navigation host
 *   exempts the browsing surface from its transition.
 * - **The page draws square.** A rounded corner is a clip, and a clip does not
 *   reach a surface. The placeholder card that stands in for a page keeps its
 *   corners; the page itself cannot have them.
 * - **Leaving the composition destroys the surface.** `onRelease` hands the
 *   view back to the [PageSurface], which is the only party that knows whether
 *   the compositor behind it can survive that. But the surface belongs to the
 *   *window*, not to the view, so disposal destroys it whatever the
 *   [PageSurface] does with the view — a caller that swaps this out on every
 *   page load is tearing down the browser's output surface on every page load,
 *   and one that swaps it out on every navigation is doing it on every
 *   navigation.
 * - **There is one call site in the product, and that is the point.**
 *   `HostedPageLayer` in the navigation host composes this once for the life of
 *   the window and never unmounts it; screens report where the page belongs
 *   through [PageSurfaceSlot] instead of composing it. A second live call site
 *   would be a second view competing for one surface, and the loser of that
 *   race is a black rectangle.
 *
 * The factory dereferences [PageSurface.attach] without a null check on
 * purpose. The contract is that a live surface always has a view, so a `null`
 * here is a broken implementation rather than a state to render around, and the
 * caller has already asked [PageSurface.isLive] before getting this far. A
 * silent fallback would turn that defect into a blank rectangle nobody can
 * explain.
 */
@Composable
fun TaffyPageSurface(surface: PageSurface, modifier: Modifier = Modifier) {
    AndroidView(
        factory = { context ->
            requireNotNull(surface.attach(context)) {
                "A live PageSurface returned no view; PageSurface.attach must not " +
                    "return null while isLive is true."
            }
        },
        onRelease = surface::detach,
        modifier = modifier,
    )
}

/**
 * The page surface in scope. Nothing live unless a host provides one.
 *
 * The default is the UI layer's answer and the fork's answer before native
 * initialization finishes: there is no engine, so there is no page. A screen
 * reads [PageSurface.isLive] and draws its placeholder instead, which is why
 * every preview and every semantics test renders without a browser under it.
 */
val LocalPageSurface = staticCompositionLocalOf { BrowserGraph.pageSurface() }

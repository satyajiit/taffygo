// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.host

import android.view.View
import androidx.activity.ComponentActivity
import androidx.compose.runtime.Composable
import androidx.compose.ui.platform.ComposeView
import androidx.lifecycle.setViewTreeLifecycleOwner
import androidx.lifecycle.setViewTreeViewModelStoreOwner
import androidx.savedstate.setViewTreeSavedStateRegistryOwner
import com.taffygo.browser.ui.app.TaffyWindowComponent
import com.taffygo.browser.ui.core.browser.PageSurface

/**
 * Turns an activity into the TaffyGo interface, as one view.
 *
 * One function, so that this is where the assumption that Chromium's activity
 * is an `androidx.activity.ComponentActivity` — and is therefore a lifecycle
 * owner, a view-model store owner and a saved-state registry owner at once — is
 * written down, in exactly one place. `TaffyBrowserActivity` extends
 * `AsyncInitializationActivity`, whose chain reaches `ComponentActivity` through
 * `ChromeBaseAppCompatActivity` and `AppCompatActivity`. The assumption is
 * compile-visible — the signature below names `ComponentActivity` — so a rebase
 * that breaks it fails this file's compile rather than throwing a
 * `ClassCastException` on a device.
 *
 * It is Java-callable on purpose. `TaffyBrowserActivity` is Java — it is the one
 * file the manifest names, and keeping it Java means the launcher entry point
 * does not depend on the Kotlin mount being healthy — so the whole of the
 * activity's side of this is `setContentView(TaffyShellViews.of(this))`.
 *
 * WHY THE VIEW TREE OWNERS ARE SET HERE. `ComposeView` finds its lifecycle,
 * store and saved-state owners by walking up the view tree. Those are normally
 * planted on the decor view by `AppCompatActivity.setContentView`, which runs
 * *after* this view is built — and a `ComposeView` that is attached before they
 * exist throws rather than waiting. Setting them on the view itself makes the
 * order irrelevant, which matters here because the activity builds its content
 * inside `triggerLayoutInflation()` rather than in `onCreate`.
 */
object TaffyShellViews {

    /**
     * The whole TaffyGo interface, ready to be the activity's content view.
     *
     * [bars] is how the resolved theme reaches the window. It is optional
     * because a test may want the tree without a window to colour; an activity
     * that omits it gets Chrome's night-qualifier polarity, which is the
     * defect [TaffyWindowBars] exists to remove — so the browser activity
     * always passes one.
     *
     * [pageSurface] is where the web page's own pixels go. It is optional for
     * the same reason and with a stronger consequence: a caller that omits it
     * gets `BrowserGraph.pageSurface()`, the surface that answers
     * `isLive == false`, and screen SCR-101 draws its placeholder card instead
     * of a page. That is the correct answer for a test or a preview and the
     * wrong one for the browser, so `TaffyBrowserActivity` passes its page host
     * through [TaffyPageSurfaceBinding]. **Once one is passed, the page host
     * belongs to the composition and must not also be added to a view group by
     * hand** — Compose's `AndroidView` adds it to a holder of its own.
     *
     * [navigation] is how an intent from another application reaches the back
     * stack this composition owns. Optional like the other two, and with a
     * consequence worth naming: a caller that omits it gets a browser that
     * opens an inbound link's tab underneath whatever screen is showing, which
     * is the defect [TaffyShellNavigation] exists to remove — so
     * `TaffyBrowserActivity` always passes one.
     */
    @JvmStatic
    @JvmOverloads
    fun of(
        activity: ComponentActivity,
        windowComponent: TaffyWindowComponent,
        bars: TaffyWindowBars? = null,
        pageSurface: PageSurface? = null,
        navigation: TaffyShellNavigation? = null,
    ): View = of(activity) {
        TaffyShellContent(
            windowComponent = windowComponent,
            defaults = activity,
            bars = bars,
            pageSurface = pageSurface,
            navigation = navigation,
        )
    }

    /**
     * The same wiring around arbitrary content, for a test that wants one
     * screen rather than the whole shell.
     *
     * Kept internal to this object: the point of the public entry point above
     * is that there is one answer to "what does the activity show", and a
     * second public door would let a caller answer it differently.
     */
    private fun of(activity: ComponentActivity, content: @Composable () -> Unit): View =
        ComposeView(activity).apply {
            setViewTreeLifecycleOwner(activity)
            setViewTreeViewModelStoreOwner(activity)
            setViewTreeSavedStateRegistryOwner(activity)
            setContent(content)
        }
}

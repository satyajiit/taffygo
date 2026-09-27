// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.host

import android.content.Context
import android.view.View
import android.view.ViewGroup
import com.taffygo.browser.ui.core.browser.PageSurface

/**
 * The fork's side of the page seam: one view the activity owns, handed to a
 * composition.
 *
 * `:core:browser` publishes [PageSurface] as `android.view.View` and nothing
 * narrower, precisely so that the UI layer can compile with no renderer behind
 * it. This is what the fork puts there — the activity's `TaffyPageHostView`,
 * which is a `FrameLayout` holding upstream's `ContentViewRenderView` and the
 * page's own content view.
 *
 * WHY A BINDING OBJECT AND NOT A CLASS THE ACTIVITY CONSTRUCTS. Two reasons,
 * and both are about where a rule is written down. The implementation stays
 * private, so no caller can hold a fork-specific page-surface type and start
 * depending on it; and `TaffyBrowserActivity` is Java, so a `@JvmStatic`
 * factory on an object is the one shape that reads naturally from both sides —
 * the same arrangement as [TaffyShellViews].
 */
object TaffyPageSurfaceBinding {

    /**
     * A [PageSurface] over [view], or a surface that reports no engine when
     * [view] is null.
     *
     * Null is the real state of this activity before `triggerLayoutInflation()`
     * has run, and a caller that passes it gets the same answer the UI layer
     * gives: `isLive == false`, and the browsing surface draws its placeholder.
     *
     * WHAT `isLive` ANSWERS HERE, AND WHAT IT DELIBERATELY DOES NOT.
     * `PageSurface`'s own contract says a fork answers `false` "before native
     * initialization has finished". This one cannot, and the reason is a
     * property of Compose rather than a shortcut. `isLive` is read during
     * composition and is a plain getter over no snapshot state, so a value that
     * changed from `false` to `true` when the compositor came up would change
     * nothing on screen: nothing would invalidate, and screen SCR-101 would
     * hold its placeholder for the life of the process. Making it observable
     * would put Compose state inside a seam whose whole purpose is that
     * `:core:browser` compiles without Compose.
     *
     * So it answers the question it can answer honestly and read once: **does
     * this build have a page host at all.** The window it does not distinguish
     * — page host built, compositor not yet up — is bounded by
     * `finishNativeInitialization()`, and in it the host draws nothing because
     * there is nothing to draw. Nothing is claimed that is not true; what is
     * lost is a placeholder during a moment when the placeholder would be the
     * only thing on the screen either way.
     */
    @JvmStatic
    fun of(view: View?): PageSurface = ActivityPageSurface(view)

    /**
     * One view, lent to whichever composition is currently showing the page.
     *
     * The view is owned by the activity for the whole of its life and is not
     * created or destroyed here. That is the point of [PageSurface.detach]
     * being the surface's decision rather than the caller's: behind this view
     * is a `SurfaceView` and a `content::Compositor`, and destroying those
     * would cost a compositor rebuild on every navigation between screens.
     *
     * **Keeping the view is not the same as keeping its surface, and this note
     * used to claim it was.** It said the no-op detach avoided "a surface
     * recreation and a first-frame delay each time a person opened settings and
     * came back". It does not. A `SurfaceView`'s surface belongs to the
     * *window*, and Compose's `AndroidView` disposal removes the hosted view
     * from the window — so the view survived, the compositor survived, and the
     * surface was destroyed anyway. That is the black frame the owner saw on
     * back navigation. The fix is one level up: the page is composed once by
     * the navigation host and placed rather than mounted, so it never leaves
     * the window at all. See `PageSurfaceSlot`.
     */
    private class ActivityPageSurface(private val view: View?) : PageSurface {

        override val isLive: Boolean
            get() = view != null

        /**
         * Hands the view over, detaching it from wherever it was first.
         *
         * The detach is not defensive tidying. Compose's `AndroidView` adds the
         * view to a holder of its own and throws `IllegalStateException` on a
         * view that already has a parent, and this view legitimately has one
         * whenever a composition is being replaced rather than disposed —
         * during a navigation between screens, both the outgoing and the
         * incoming node exist for a frame. Upstream does the same thing at the
         * same seam: `TaffyPageHostView.showPage` re-parents the content view
         * for exactly this reason.
         */
        override fun attach(context: Context): View? {
            val hosted = view ?: return null
            (hosted.parent as? ViewGroup)?.removeView(hosted)
            return hosted
        }

        /** Nothing to do: the activity keeps the page host, compositor and all. */
        override fun detach(view: View) = Unit
    }
}

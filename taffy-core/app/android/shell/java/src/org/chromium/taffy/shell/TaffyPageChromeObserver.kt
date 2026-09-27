// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.shell

import com.taffygo.browser.ui.core.browser.PageAppearance
import com.taffygo.browser.ui.core.browser.PageChromeScroll
import com.taffygo.browser.ui.core.browser.PageColor
import com.taffygo.browser.ui.core.browser.StatusBarContrast
import org.chromium.base.ContextUtils
import org.chromium.base.lifetime.Destroyable
import org.chromium.chrome.browser.tab.Tab
import org.chromium.chrome.browser.tabmodel.TabModelSelector
import org.chromium.chrome.browser.tabmodel.TabModelSelectorTabObserver
import org.chromium.content_public.browser.GestureListenerManager
import org.chromium.content_public.browser.GestureStateListener
import org.chromium.content_public.browser.RenderCoordinates
import org.chromium.content_public.browser.WebContents
import org.chromium.url.GURL

/**
 * Page colour and chrome-hide, observed from the selected tab.
 *
 * [ChromiumBrowserMediator] owns the projection of tabs and navigation; this
 * owns the two facts that are not a navigation. Scroll callbacks would
 * otherwise rebuild the address pill on every finger movement, and the page
 * background would otherwise have nowhere to go — Compose chrome sits
 * outside Chromium's own browser-controls stack.
 *
 * ## Hide, and what brings the chrome back
 *
 * The rule itself is [PageChromeScroll], which is portable, has no Chromium
 * type in it and is covered by a host suite. This half is the wiring: which
 * engine callback means what, and where the facts the rule needs come from. It
 * is worth keeping those apart, because every one of the three defects fixed
 * on 2026-09-07 was in the wiring rather than in the rule, and none of them
 * could be reproduced anywhere a test could run.
 *
 * Two of those are worth stating here, because they are properties of the
 * engine's callbacks rather than of any code in this repository:
 *
 *  - **A fling's reported direction is always "down".** `onFlingStart` reads
 *    `data.scroll_begin.delta_y_hint` off a fling-start event, and no producer
 *    writes that union member for that event — what it reads is the zero the
 *    union was cleared to. So a flick towards the top of the page arrived here
 *    claiming to be a scroll away from it, which is why the chrome vanished on
 *    a gesture whose whole purpose was to bring it back. This class no longer
 *    reads that argument.
 *  - **A scroll gesture is acknowledged before anything asks whether the page
 *    could consume it.** `GestureListenerManager` calls `onScrollBegin` and
 *    returns, above the line that works out whether the scroll was consumed,
 *    so a swipe against the end of a document with nothing to scroll is
 *    reported exactly like a scroll down a long one. Only the rule can tell
 *    them apart, and only from [RenderCoordinates].
 *
 * A tab that has been nowhere always shows the chrome: there is no page to
 * reclaim the space from, and the start content is TaffyGo's own.
 *
 * `PageAppearance.topBarVisible` is the field this writes, and it governs the
 * top bar alone: the action row at the bottom of the window is always on
 * screen and answers to nothing here. Visibility is published only when it
 * actually changes — this observer exists so scroll callbacks do not rebuild
 * the address pill every time the engine speaks.
 */
class TaffyPageChromeObserver(
    private val selector: TabModelSelector,
    private val publish: (PageAppearance) -> Unit,
) : Destroyable {

    private val scroll = PageChromeScroll(PageChromeScroll.Bounds.ofChrome(barHeightPx()))
    private var lastColor: Int? = null
    private var boundContents: WebContents? = null

    private val tabObserver = object : TabModelSelectorTabObserver(selector) {
        override fun onShown(tab: Tab, type: Int) {
            if (tab !== selector.currentTab) return
            scroll.restarted()
            refresh()
        }

        override fun onContentChanged(tab: Tab) {
            if (tab === selector.currentTab) refresh()
        }

        override fun onDidChangeThemeColor(tab: Tab, color: Int) {
            if (tab === selector.currentTab) emit(tab)
        }

        override fun onBackgroundColorChanged(tab: Tab, color: Int) {
            if (tab === selector.currentTab) emit(tab)
        }

        // A page that arrives while the chrome is hidden brings it back with
        // it. Without this the reader meets a new document with no address on
        // the screen and no gesture that is guaranteed to produce one, because
        // a fresh page starts at the top of itself.
        override fun onPageLoadFinished(tab: Tab, url: GURL) {
            if (tab !== selector.currentTab) return
            scroll.restarted()
            refresh()
        }

        override fun onUrlUpdated(tab: Tab) {
            if (tab !== selector.currentTab) return
            scroll.restarted()
            refresh()
        }
    }

    private val gestures = object : GestureStateListener() {
        // The one callback allowed to hide the top bar, because it is the one
        // raised by a finger that actually began a scroll and the one whose
        // reported direction can be believed. Neither argument it carries is
        // usable: `scrollExtentY` is the viewport's own height, the same on a
        // page of one line and a page of a thousand, and `scrollOffsetY` is
        // whatever the engine last had reason to report, which mid-page is not
        // where the reader is. The rule is handed what it actually needs.
        override fun onScrollStarted(
            scrollOffsetY: Int,
            scrollExtentY: Int,
            isDirectionUp: Boolean,
        ) = settle(scroll.began(reachable(), isDirectionUp))

        // Deliberately not routed. See this class's own note: the direction
        // this argument carries is a zero that was never written, so every
        // fling reads as a scroll down the page — including the flick that was
        // asking for the chrome back. A fling that follows a drag has already
        // been answered by that drag.
        override fun onFlingStartGesture(
            scrollOffsetY: Int,
            scrollExtentY: Int,
            isDirectionUp: Boolean,
        ) = Unit

        override fun onVerticalScrollDirectionChanged(
            directionUp: Boolean,
            currentScrollRatio: Float,
        ) = settle(scroll.turned(directionUp))

        override fun onScrollOffsetOrExtentChanged(scrollOffsetY: Int, scrollExtentY: Int) =
            settle(scroll.scrolled(scrollOffsetY, reachable()))

        override fun onSingleTap(consumed: Boolean) = settle(scroll.tapped())

        override fun onDestroyed() {
            boundContents = null
        }
    }

    /**
     * How much further the document could be scrolled, in physical pixels.
     *
     * The document's height less the viewport's, which is the only form of
     * "can this page scroll" the engine will answer. Answered as nothing at
     * all when there is no page or the measurement has not been taken yet, so
     * a page whose layout has not settled is one the chrome stays up over
     * rather than one it guesses about.
     */
    private fun reachable(): Int {
        val contents = boundContents ?: return 0
        return RenderCoordinates.fromWebContents(contents).maxVerticalScrollPixInt
            .coerceAtLeast(0)
    }

    init {
        refresh()
    }

    override fun destroy() {
        unbind()
        tabObserver.destroy()
    }

    /**
     * Publish, but only when the rule actually moved.
     *
     * The engine speaks far more often than the answer changes, and the
     * appearance flow feeds the address pill; a publication per callback would
     * rebuild that pill through a whole gesture. A tab that has been nowhere
     * needs no publication either, because [emit] holds the chrome up over the
     * start page whatever the rule has decided about a page underneath it.
     */
    private fun settle(changed: Boolean) {
        if (changed) emit(selector.currentTab)
    }

    private fun refresh() {
        val tab = selector.currentTab
        bind(tab?.webContents)
        emit(tab)
    }

    private fun emit(tab: Tab?) {
        if (tab == null) {
            lastColor = null
            publish(PageAppearance())
            return
        }
        val nowhere = TaffyNavigationProjection.hasBeenNowhere(committedSpec(tab))
        val resolved = PageColor.of(
            hasBeenNowhere = nowhere,
            backgroundArgb = tab.backgroundColor,
            themeArgb = tab.themeColor,
            themingAllowed = tab.isThemingAllowed,
        )
        val color = resolved?.let { StatusBarContrast.stabilize(it, lastColor) }
        lastColor = color
        publish(
            PageAppearance(
                backgroundArgb = if (nowhere) null else color,
                topBarVisible = nowhere || scroll.topBarVisible,
            ),
        )
    }

    private fun bind(contents: WebContents?) {
        if (contents === boundContents) return
        unbind()
        if (contents == null) return
        // Held before the listener is added, not after. Adding one calls it
        // back with the current scroll offset inside `addListener`, and
        // [reachable] answers from this field — so assigning afterwards would
        // measure the page that is arriving against no page at all.
        boundContents = contents
        GestureListenerManager.fromWebContents(contents)?.addListener(gestures)
    }

    private fun unbind() {
        val contents = boundContents ?: return
        GestureListenerManager.fromWebContents(contents)?.removeListener(gestures)
        boundContents = null
    }

    /**
     * What has actually committed in this tab, as a spec.
     *
     * Same question [ChromiumBrowserMediator] asks, asked here because a
     * theme-colour callback can fire on a tab that has not committed yet
     * and must not colour the start content as if it were a page.
     */
    private fun committedSpec(tab: Tab): String {
        val contents = tab.webContents ?: return tab.url.spec
        return contents.lastCommittedUrl.spec
    }

    private companion object {
        /**
         * One bar of chrome in density-independent units.
         *
         * `ActionRowHeight` in the browsing feature's `BrowserChromeControls`,
         * which this layer may not name — a shell may reach a portable UI
         * module and never a feature. Written here rather than passed in
         * because a number a caller could get wrong is worse than a number
         * that is stated twice with a note saying where the other copy is.
         */
        const val BAR_HEIGHT_DP = 56f

        /** That height in this display's pixels, for the rule's regions. */
        fun barHeightPx(): Int {
            val density = ContextUtils.getApplicationContext()
                .resources
                .displayMetrics
                .density
            return (BAR_HEIGHT_DP * density).toInt().coerceAtLeast(1)
        }
    }
}

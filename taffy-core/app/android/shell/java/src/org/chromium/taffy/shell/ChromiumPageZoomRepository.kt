// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.shell

import androidx.annotation.VisibleForTesting
import com.taffygo.browser.ui.core.common.AppDispatchers
import com.taffygo.browser.ui.feature.browsing.PageZoomRepository
import com.taffygo.browser.ui.feature.browsing.PageZoomState
import java.io.Closeable
import kotlin.math.abs
import kotlin.math.pow
import kotlin.math.roundToInt
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.withContext
import org.chromium.base.ThreadUtils
import org.chromium.base.lifetime.Destroyable
import org.chromium.chrome.browser.ZoomController
import org.chromium.chrome.browser.profiles.Profile
import org.chromium.chrome.browser.tab.Tab
import org.chromium.chrome.browser.tabmodel.TabModelSelector
import org.chromium.chrome.browser.tabmodel.TabModelSelectorTabModelObserver
import org.chromium.chrome.browser.tabmodel.TabModelSelectorTabObserver
import org.chromium.content_public.browser.HostZoomMap
import org.chromium.content_public.browser.WebContents

/** Chromium's persistent per-host layout zoom, projected for the selected tab only. */
class ChromiumPageZoomRepository @VisibleForTesting constructor(
    private val environment: Environment,
    private val dispatchers: AppDispatchers,
) : PageZoomRepository, Destroyable, Closeable {

    /** Browser lookup and observation seam for native-free tests. */
    @VisibleForTesting
    interface Environment {
        fun selectedPage(): Page?
        fun observe(onChanged: () -> Unit): Closeable
    }

    /** One live selected WebContents, resolved afresh for every action. */
    @VisibleForTesting
    interface Page {
        val zoomFactor: Double
        val defaultZoomFactor: Double
        fun zoomIn()
        fun zoomOut()
        fun reset()
    }

    constructor(
        profile: Profile,
        selector: TabModelSelector,
        dispatchers: AppDispatchers,
    ) : this(ChromiumEnvironment(profile, selector), dispatchers)

    private val mutableState = MutableStateFlow(PageZoomState())
    private var closed = false
    private val observation = environment.observe(::refresh)

    override val state: StateFlow<PageZoomState> = mutableState.asStateFlow()

    init {
        refresh()
    }

    override suspend fun zoomIn() = act(Page::zoomIn)

    override suspend fun zoomOut() = act(Page::zoomOut)

    override suspend fun reset() = act(Page::reset)

    private suspend fun act(action: Page.() -> Unit) {
        withContext(dispatchers.main) {
            if (closed) return@withContext
            environment.selectedPage()?.action()
            refresh()
        }
    }

    private fun refresh() {
        if (closed) return
        val page = environment.selectedPage()
        mutableState.value = page?.toState() ?: PageZoomState()
    }

    override fun destroy() {
        if (closed) return
        closed = true
        observation.close()
        mutableState.value = PageZoomState()
    }

    override fun close() = destroy()

    private fun Page.toState(): PageZoomState {
        val factor = zoomFactor
        val profileDefault = defaultZoomFactor
        if (!factor.isFinite() || !profileDefault.isFinite()) return PageZoomState()
        val minimum = HostZoomMap.AVAILABLE_ZOOM_FACTORS.first()
        val maximum = HostZoomMap.AVAILABLE_ZOOM_FACTORS.last()
        return PageZoomState(
            available = true,
            percent = (HostZoomMap.TEXT_SIZE_MULTIPLIER_RATIO.toDouble().pow(factor) * 100.0)
                .roundToInt()
                .coerceIn(MINIMUM_PERCENT, MAXIMUM_PERCENT),
            canZoomOut = factor > minimum + FACTOR_EPSILON,
            canZoomIn = factor < maximum - FACTOR_EPSILON,
            canReset = abs(factor - profileDefault) > FACTOR_EPSILON,
        )
    }

    private class ChromiumEnvironment(
        private val profile: Profile,
        private val selector: TabModelSelector,
    ) : Environment {
        override fun selectedPage(): Page? {
            ThreadUtils.assertOnUiThread()
            val tab = selector.currentTab ?: return null
            if (tab.isDestroyed || tab.isClosing) return null
            val contents = tab.webContents ?: return null
            if (contents.isDestroyed || !contents.lastCommittedUrl.isValid) return null
            return ChromiumPage(tab, contents)
        }

        override fun observe(onChanged: () -> Unit): Closeable {
            ThreadUtils.assertOnUiThread()
            val modelObserver = object : TabModelSelectorTabModelObserver(selector) {
                override fun didSelectTab(tab: Tab, type: Int, lastId: Int) = onChanged()
                override fun restoreCompleted() = onChanged()
            }
            val tabObserver = object : TabModelSelectorTabObserver(selector) {
                override fun onUrlUpdated(tab: Tab) {
                    if (tab === selector.currentTab) onChanged()
                }

                override fun onPageLoadFinished(tab: Tab, url: org.chromium.url.GURL) {
                    if (tab === selector.currentTab) onChanged()
                }

                override fun onCrash(tab: Tab) {
                    if (tab === selector.currentTab) onChanged()
                }
            }
            val zoomSubscription = HostZoomMap.addZoomLevelObserver(profile) { onChanged() }
            return Closeable {
                ThreadUtils.assertOnUiThread()
                tabObserver.destroy()
                modelObserver.destroy()
                if (zoomSubscription >= 0) {
                    HostZoomMap.removeZoomLevelObserver(profile, zoomSubscription)
                }
            }
        }
    }

    private class ChromiumPage(
        private val tab: Tab,
        private val contents: WebContents,
    ) : Page {
        override val zoomFactor: Double
            get() = HostZoomMap.getZoomLevel(contents)

        override val defaultZoomFactor: Double
            get() = HostZoomMap.getDefaultZoomLevel(tab.profile)

        override fun zoomIn() {
            ZoomController.zoomInPage(contents)
        }

        override fun zoomOut() {
            ZoomController.zoomOutPage(contents)
        }

        override fun reset() {
            ZoomController.zoomResetPage(contents, tab.profile)
        }
    }

    private companion object {
        const val FACTOR_EPSILON = 0.005
        const val MINIMUM_PERCENT = 50
        const val MAXIMUM_PERCENT = 300
    }
}

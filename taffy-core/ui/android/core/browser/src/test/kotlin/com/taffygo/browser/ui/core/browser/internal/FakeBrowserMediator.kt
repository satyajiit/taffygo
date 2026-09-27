// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.browser.internal

import android.graphics.Bitmap
import com.taffygo.browser.ui.core.browser.BrowserMediator
import com.taffygo.browser.ui.core.browser.FilteringSettings
import com.taffygo.browser.ui.core.browser.NavigationState
import com.taffygo.browser.ui.core.browser.PageAppearance
import com.taffygo.browser.ui.core.browser.SiteFilteringPlane
import com.taffygo.browser.ui.core.browser.TabArtwork
import com.taffygo.browser.ui.core.common.Clock
import com.taffygo.browser.ui.core.model.DownloadAction
import com.taffygo.browser.ui.core.model.DownloadId
import com.taffygo.browser.ui.core.model.DownloadRecord
import com.taffygo.browser.ui.core.model.DownloadState
import com.taffygo.browser.ui.core.model.PageLoadFailure
import com.taffygo.browser.ui.core.model.Tab
import com.taffygo.browser.ui.core.model.TabId
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock

/**
 * Tabs, navigation, and downloads with no web engine behind them.
 *
 * The UI host has no renderer, so this fake never claims a page loaded. It
 * models the state transitions the browser surfaces have to handle — selection,
 * closing the last tab, going back, a page that failed — and the hosts it uses
 * are the deterministic fixture hosts, never live sites.
 */
internal class FakeBrowserMediator(
    private val clock: Clock,
) : BrowserMediator {

    private val mutex = Mutex()
    private val tabState = MutableStateFlow(SeedContent.tabs)
    private val navigationState = MutableStateFlow(SeedContent.navigation)
    private val downloadState = MutableStateFlow(SeedContent.downloads)
    private val artworkState = MutableStateFlow(emptyMap<TabId, TabArtwork>())
    private val history = ArrayDeque<String>()
    private val forwardHistory = ArrayDeque<String>()

    override val tabs: StateFlow<List<Tab>> = tabState.asStateFlow()
    override val navigation: StateFlow<NavigationState> = navigationState.asStateFlow()
    override val pageAppearance: StateFlow<PageAppearance> =
        MutableStateFlow(PageAppearance()).asStateFlow()
    override val downloads: StateFlow<List<DownloadRecord>> = downloadState.asStateFlow()
    override val tabArtwork: StateFlow<Map<TabId, TabArtwork>> = artworkState.asStateFlow()

    private val siteMarksState = MutableStateFlow(emptyMap<String, Bitmap>())

    override val siteMarks: StateFlow<Map<String, Bitmap>> = siteMarksState.asStateFlow()

    /** There is no engine and so no favicon store; asking is a recorded no-op. */
    override suspend fun requestSiteMarks(hosts: Collection<String>) = Unit

    override suspend fun openTab(host: String, isPrivate: Boolean): TabId = mutex.withLock {
        val id = TabId("tab_${clock.nowEpochMillis()}_${tabState.value.size}")
        tabState.value = tabState.value.map { it.copy(isSelected = false) } +
            Tab(
                id = id,
                title = host,
                host = host,
                // The one place this fake can answer honestly. A tab opened on
                // no address has been nowhere, which is exactly what the start
                // page asks for when it opens a tab before the address bar
                // (`NewTabViewModel.NOWHERE_YET`), and the browsing surface
                // draws its own start content rather than an empty page area.
                hasBeenNowhere = host.isBlank(),
                isPrivate = isPrivate,
                isSelected = true,
                openedAtEpochMillis = clock.nowEpochMillis(),
            )
        show(host)
        id
    }

    override suspend fun selectTab(id: TabId) = mutex.withLock {
        forwardHistory.clear()
        tabState.value = tabState.value.map { it.copy(isSelected = it.id == id) }
        tabState.value.firstOrNull { it.isSelected }?.let { show(it.host) }
        Unit
    }

    override suspend fun closeTab(id: TabId) = mutex.withLock {
        forwardHistory.clear()
        val remaining = tabState.value.filterNot { it.id == id }
        // A browser always has a tab. Closing the last one opens a new one
        // rather than leaving the window empty.
        tabState.value = remaining.ifEmpty { listOf(SeedContent.newTab(clock.nowEpochMillis())) }
        if (tabState.value.none { it.isSelected }) {
            tabState.value = tabState.value.mapIndexed { index, tab -> tab.copy(isSelected = index == 0) }
        }
        tabState.value.first { it.isSelected }.let { show(it.host) }
    }

    override suspend fun navigateTo(address: String) = mutex.withLock {
        val selected = tabState.value.firstOrNull { it.isSelected }
        // A tab that has been nowhere is still on the blank document. The first
        // typed address replaces that entry, so system back leaves the site
        // rather than returning to an empty tab.
        if (selected?.hasBeenNowhere != true) {
            history.addLast(navigationState.value.host)
        }
        forwardHistory.clear()
        tabState.value = tabState.value.map {
            if (it.isSelected) {
                it.copy(host = address, title = address, hasBeenNowhere = address.isBlank())
            } else {
                it
            }
        }
        show(address)
    }

    override suspend fun goBack(): Boolean = mutex.withLock {
        val previous = history.removeLastOrNull() ?: return@withLock false
        forwardHistory.addLast(navigationState.value.host)
        show(previous)
        true
    }

    override suspend fun goForward(): Boolean = mutex.withLock {
        val next = forwardHistory.removeLastOrNull() ?: return@withLock false
        history.addLast(navigationState.value.host)
        show(next)
        true
    }

    override suspend fun reload() = mutex.withLock {
        show(navigationState.value.host)
    }

    override suspend fun performDownloadAction(id: DownloadId, action: DownloadAction): Boolean =
        mutex.withLock {
            val current = downloadState.value.firstOrNull { it.id == id } ?: return@withLock false
            if (action !in current.allowedActions) return@withLock false
            val changed = when (action) {
                DownloadAction.PAUSE -> current.copy(
                    state = DownloadState.PAUSED,
                    allowedActions = setOf(DownloadAction.RESUME, DownloadAction.CANCEL),
                )
                DownloadAction.RESUME -> current.copy(
                    state = DownloadState.RUNNING,
                    allowedActions = setOf(DownloadAction.PAUSE, DownloadAction.CANCEL),
                )
                DownloadAction.CANCEL,
                DownloadAction.OPEN,
                DownloadAction.SHARE,
                DownloadAction.REMOVE,
                -> return@withLock false
            }
            downloadState.value = downloadState.value.map { download ->
                if (download.id == id) changed else download
            }
            true
        }

    private val filteringState = MutableStateFlow(FilteringSettings())

    override val filtering: StateFlow<FilteringSettings> = filteringState.asStateFlow()

    override suspend fun setFilteringEnabled(enabled: Boolean) = mutex.withLock {
        filteringState.value = filteringState.value.copy(enabled = enabled)
        show(navigationState.value.host)
    }

    /**
     * The same refusal shape the browser has: a host, never an address. The
     * rule itself lives browser-side; what this fake models is that a refused
     * host records nothing and the caller is told.
     */
    override suspend fun setSiteFilteringException(
        host: String,
        allow: Boolean,
        plane: SiteFilteringPlane,
    ): Boolean =
        mutex.withLock {
            if (host.isBlank() || host.any { it == '/' || it == ':' || it.isWhitespace() }) {
                return@withLock false
            }
            val current = filteringState.value
            val hosts = if (allow) {
                (current.exceptionHosts + host).distinct()
            } else {
                current.exceptionHosts - host
            }
            filteringState.value = current.copy(exceptionHosts = hosts)
            show(navigationState.value.host)
            true
        }

    /** Nothing is ever held back here: the count is always current, at zero. */
    override suspend fun flushFilteringCounts() = Unit

    /**
     * There is no engine, so a host either resolves to a fixture page or does
     * not resolve at all. Saying which is the honest half of having no engine.
     */
    private fun show(host: String) {
        val failure = SeedContent.failureFor(host)
        val settings = filteringState.value
        val excepted = host.isNotBlank() &&
            settings.exceptionHosts.any { host == it || host.endsWith(".$it") }
        navigationState.value = NavigationState(
            host = host,
            title = SeedContent.titleFor(host),
            canGoBack = history.isNotEmpty(),
            canGoForward = forwardHistory.isNotEmpty(),
            isLoading = false,
            failure = failure,
            // The posture transitions are modelled; the count stays zero,
            // because with no engine nothing was blocked and zero is the
            // honest number. There is one profile here, so the browser's
            // per-plane answer and this list agree by construction.
            filteringActive = host.isNotBlank() && settings.enabled && !excepted,
            siteExcepted = excepted,
        )
    }
}

/** The unreachable host the UI host uses to reach the error page. */
internal val UNREACHABLE_HOST_FAILURE: PageLoadFailure = PageLoadFailure.NAME_NOT_RESOLVED

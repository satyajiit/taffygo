// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.browser

import android.graphics.Bitmap
import com.taffygo.browser.ui.core.model.DownloadAction
import com.taffygo.browser.ui.core.model.DownloadId
import com.taffygo.browser.ui.core.model.DownloadRecord
import com.taffygo.browser.ui.core.model.Tab
import com.taffygo.browser.ui.core.model.TabId
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow

private val INCOMPLETE_DOWNLOAD_PROJECTION: StateFlow<Boolean> =
    MutableStateFlow(false)

/**
 * The browser-mediator seam (android-app-architecture section 5).
 *
 * Upstream Chromium owns tabs, navigation, and downloads. This interface is the
 * whole of what a Taffy-owned surface may ask of them. A screen cannot reach
 * past this port into browser internals, because no browser object crosses it.
 * Chromium supplies the production implementation at Window construction.
 */
interface BrowserMediator {

    /** Every open tab, the user's and Taffy's, in the order the switcher shows them. */
    val tabs: StateFlow<List<Tab>>

    /** What the selected tab is showing. */
    val navigation: StateFlow<NavigationState>

    /**
     * How the selected page should colour the window, and whether the bottom
     * action row is showing.
     *
     * Separate from [navigation] because scroll callbacks are not a
     * navigation: they should not rebuild the address pill.
     */
    val pageAppearance: StateFlow<PageAppearance>

    /** At most 256 downloads, newest first, in the browser's own chronology. */
    val downloads: StateFlow<List<DownloadRecord>>

    /** Whether the initial profile download query has answered. */
    val downloadsReady: StateFlow<Boolean>
        get() = INCOMPLETE_DOWNLOAD_PROJECTION

    /** True only after the bounded projection proved it saw the complete profile store. */
    val downloadsComplete: StateFlow<Boolean>
        get() = INCOMPLETE_DOWNLOAD_PROJECTION

    /** True when the browser's initial profile-store read failed rather than merely being partial. */
    val downloadsUnavailable: StateFlow<Boolean>
        get() = INCOMPLETE_DOWNLOAD_PROJECTION

    /**
     * Local engine bitmaps for the tab switcher, keyed by tab.
     *
     * Empty until the engine has a favicon or a snapshot. A missing entry is
     * the same as [TabArtwork] with both fields null.
     */
    val tabArtwork: StateFlow<Map<TabId, TabArtwork>>

    /**
     * The engine's stored favicon for each host it has been asked about.
     *
     * Answers to [requestSiteMarks], from the profile's own favicon store —
     * marks the engine saved while the person visited those sites, so a host
     * appears here without needing an open tab. Never a network fetch, and a
     * host the store has no mark for is simply absent.
     */
    val siteMarks: StateFlow<Map<String, Bitmap>>

    /**
     * Ask the local favicon store for the marks of [hosts].
     *
     * Results arrive on [siteMarks] as the store answers. Idempotent and
     * cheap to repeat: a host already answered is not asked again, and one
     * the store had nothing for is retried on the next request, because the
     * store gains marks as the person browses.
     */
    suspend fun requestSiteMarks(hosts: Collection<String>)

    /** Open a new tab on [host] and select it. */
    suspend fun openTab(host: String, isPrivate: Boolean = false): TabId

    /**
     * Open the third-party notices this package carries in a new tab, and select it.
     *
     * It takes no address on purpose. The notice is an engine page, and every other way into
     * the browser refuses engine pages (decision 0154); the browser supplies the address
     * itself, so this is not a way to open any other page (decision 0206). False means nothing
     * was opened.
     */
    suspend fun openAttributionNotice(): Boolean = false

    /** Select an existing tab. */
    suspend fun selectTab(id: TabId)

    /** Check exact live task attribution and select atomically; missing support refuses. */
    suspend fun selectTaskTab(id: TabId, taskId: String): Boolean = false

    /** Exact regular tabs the browser currently admits as sources for this task. */
    suspend fun tabsForTask(taskId: String): Set<TabId> = emptySet()

    /** Close a tab. Closing the last user tab opens a new one, as a browser does. */
    suspend fun closeTab(id: TabId)

    /** Navigate the selected tab to [address]. */
    suspend fun navigateTo(address: String)

    /** Go back in the selected tab. Returns false when there is no history. */
    suspend fun goBack(): Boolean

    /** Go forward in the selected tab. Returns false when there is no history ahead. */
    suspend fun goForward(): Boolean

    /** Ask for the failed page again. */
    suspend fun reload()

    /** Stop the selected tab's current load. False means there was no live load to stop. */
    suspend fun stopLoading(): Boolean = false

    /**
     * Perform one action Chromium advertised for the exact download currently shown.
     *
     * Implementations must recheck the live item before acting and report whether the request was
     * handed to Chromium. There is deliberately no fire-and-forget pause Boolean beside this
     * method: such a port cannot distinguish an accepted control from a stale row.
     */
    suspend fun performDownloadAction(id: DownloadId, action: DownloadAction): Boolean

    /** Current safe completed files that this exact task started in this browser session. */
    suspend fun completedTaskDownloads(taskId: String): List<DownloadRecord> = emptyList()

    /** Rechecks attribution and file state, then hands the person's Open request to Chromium. */
    suspend fun openTaskDownload(taskId: String, id: DownloadId): Boolean = false

    /**
     * The profile's ad- and tracker-blocking configuration and lifetime
     * total. The selected page's live facts ride [navigation] instead.
     */
    val filtering: StateFlow<FilteringSettings>

    /** Turn blocking on or off for the whole profile. */
    suspend fun setFilteringEnabled(enabled: Boolean)

    /**
     * Record or remove one site exception. [host] is a host, never an
     * address; false means the browser refused to record it and nothing
     * changed.
     */
    suspend fun setSiteFilteringException(
        host: String,
        allow: Boolean,
        plane: SiteFilteringPlane,
    ): Boolean

    /**
     * Publish whatever the selected tab's blocked-count coalescer is
     * holding, for the moment the number is about to be looked at — the
     * site sheet opening. The count arrives on [navigation] as usual.
     */
    suspend fun flushFilteringCounts()
}

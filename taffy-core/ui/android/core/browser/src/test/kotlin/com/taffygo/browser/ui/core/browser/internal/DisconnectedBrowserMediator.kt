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
import com.taffygo.browser.ui.core.model.DownloadAction
import com.taffygo.browser.ui.core.model.DownloadId
import com.taffygo.browser.ui.core.model.DownloadRecord
import com.taffygo.browser.ui.core.model.Tab
import com.taffygo.browser.ui.core.model.TabId
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow

/**
 * The browser seam before there is a browser behind it.
 *
 * A shipping build reaches this state for real: the Compose shell is built in
 * `triggerLayoutInflation()`, and the tab model cannot exist until native
 * initialization has finished. Something has to answer [BrowserMediator]
 * across that window, and what it answers is what the user sees.
 *
 * ## Why this is not [FakeBrowserMediator]
 *
 * The fixture implementation holds
 * seeded fixture tabs on `docs.example.test` and friends, so a screen can be
 * driven through selection, closing the last tab and a page that failed without
 * a renderer. Every one of those is a claim — "you have three tabs open, and
 * this one is showing a retention policy" — and it is a claim a build with a
 * real browser in it has no business making. A shipping surface that briefly
 * showed fixture tabs would be lying about the user's own session for as long
 * as it took native to come up.
 *
 * So this reports nothing at all. No tabs, a blank [NavigationState], no
 * downloads, no history to go back through, and every command a no-op. That is
 * the honest description of a browser that is not there yet, and it is the
 * repository's honesty rule applied to a seam rather than to a gate: nothing
 * here reports a result it did not obtain.
 *
 * It is deliberately not injectable and exists in the test source set only.
 */
internal class DisconnectedBrowserMediator : BrowserMediator {

    private val noTabs = MutableStateFlow(emptyList<Tab>())
    private val nothingShown = MutableStateFlow(NavigationState(host = "", title = ""))
    private val defaultAppearance = MutableStateFlow(PageAppearance())
    private val noDownloads = MutableStateFlow(emptyList<DownloadRecord>())
    private val noArtwork = MutableStateFlow(emptyMap<TabId, TabArtwork>())
    private val noMarks = MutableStateFlow(emptyMap<String, Bitmap>())

    override val tabs: StateFlow<List<Tab>> = noTabs

    override val navigation: StateFlow<NavigationState> = nothingShown

    override val pageAppearance: StateFlow<PageAppearance> = defaultAppearance

    override val downloads: StateFlow<List<DownloadRecord>> = noDownloads

    override val tabArtwork: StateFlow<Map<TabId, TabArtwork>> = noArtwork

    /** No favicon store to read yet, so no host is ever answered. */
    override val siteMarks: StateFlow<Map<String, Bitmap>> = noMarks

    override suspend fun requestSiteMarks(hosts: Collection<String>) = Unit

    /**
     * No tab is opened, and the identifier says so.
     *
     * The signature has to return one, so it returns [NO_TAB] — an id that
     * names nothing and matches nothing in [tabs], which stays empty. A caller
     * that looks the id up finds no tab, which is the truth.
     */
    override suspend fun openTab(host: String, isPrivate: Boolean): TabId = NO_TAB

    override suspend fun selectTab(id: TabId) = Unit

    override suspend fun closeTab(id: TabId) = Unit

    override suspend fun navigateTo(address: String) = Unit

    /** There is no history, so back has nowhere to go and says so. */
    override suspend fun goBack(): Boolean = false

    /** There is no history ahead either. */
    override suspend fun goForward(): Boolean = false

    override suspend fun reload() = Unit

    override suspend fun performDownloadAction(
        id: DownloadId,
        action: DownloadAction,
    ): Boolean = false

    private val noFiltering = MutableStateFlow(FilteringSettings())

    /**
     * The defaults, which are the only honest answer: the real toggle lives
     * in a profile preference this build has not loaded yet, and defaults
     * are what that preference holds until somebody changes it.
     */
    override val filtering: StateFlow<FilteringSettings> = noFiltering

    /** No browser to configure; the command changes nothing. */
    override suspend fun setFilteringEnabled(enabled: Boolean) = Unit

    /** No browser to record it; the refusal is the honest answer. */
    override suspend fun setSiteFilteringException(
        host: String,
        allow: Boolean,
        plane: SiteFilteringPlane,
    ): Boolean = false

    /** No coalescer to flush. */
    override suspend fun flushFilteringCounts() = Unit

    internal companion object {
        /** The identifier of the tab that was not opened. */
        internal val NO_TAB = TabId("")
    }
}

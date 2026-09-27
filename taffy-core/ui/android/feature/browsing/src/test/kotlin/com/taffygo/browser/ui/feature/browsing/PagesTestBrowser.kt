// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import android.graphics.Bitmap
import com.taffygo.browser.ui.core.browser.BrowserRepository
import com.taffygo.browser.ui.core.browser.FilteringSettings
import com.taffygo.browser.ui.core.browser.NavigationState
import com.taffygo.browser.ui.core.browser.PageAppearance
import com.taffygo.browser.ui.core.browser.SiteFilteringPlane
import com.taffygo.browser.ui.core.browser.TabArtwork
import com.taffygo.browser.ui.core.model.AddressBarInterpretation
import com.taffygo.browser.ui.core.model.BrowserNotice
import com.taffygo.browser.ui.core.model.DownloadAction
import com.taffygo.browser.ui.core.model.DownloadId
import com.taffygo.browser.ui.core.model.DownloadRecord
import com.taffygo.browser.ui.core.model.Suggestion
import com.taffygo.browser.ui.core.model.Tab
import com.taffygo.browser.ui.core.model.TabId
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow

/** Records tab opens, selects, closes and favicon asks; invents nothing else. */
class PagesTestBrowser : BrowserRepository {
    val opened = mutableListOf<Pair<String, Boolean>>()
    val selected = mutableListOf<TabId>()
    val closed = mutableListOf<TabId>()
    val markRequests = mutableListOf<List<String>>()

    private val tabsFlow = MutableStateFlow<List<Tab>>(emptyList())
    override val tabs: StateFlow<List<Tab>> = tabsFlow

    /** What the engine's tab list now says. */
    fun showTabs(tabs: List<Tab>) {
        tabsFlow.value = tabs
    }
    override val navigation: StateFlow<NavigationState> =
        MutableStateFlow(NavigationState(host = "", title = ""))
    override val pageAppearance: StateFlow<PageAppearance> = MutableStateFlow(PageAppearance())
    override val downloads: StateFlow<List<DownloadRecord>> = MutableStateFlow(emptyList())
    override val tabArtwork: StateFlow<Map<TabId, TabArtwork>> = MutableStateFlow(emptyMap())
    override val siteMarks: StateFlow<Map<String, Bitmap>> = MutableStateFlow(emptyMap())
    override val notice: StateFlow<BrowserNotice?> = MutableStateFlow(null)
    override val filtering: StateFlow<FilteringSettings> = MutableStateFlow(FilteringSettings())

    override suspend fun requestSiteMarks(hosts: Collection<String>) {
        markRequests += hosts.toList()
    }

    override fun resolve(input: String): AddressBarInterpretation =
        AddressBarInterpretation.GoTo(input, input)

    override fun suggestions(input: String): List<Suggestion> = emptyList()
    override suspend fun commit(interpretation: AddressBarInterpretation) = Unit
    override fun dismissNotice() = Unit
    override suspend fun selectTab(id: TabId) {
        selected += id
    }
    override suspend fun closeTab(id: TabId) {
        closed += id
    }
    override suspend fun openTab(host: String, isPrivate: Boolean): TabId {
        opened += host to isPrivate
        return TabId("tab_${opened.size}")
    }
    override suspend fun goBack(): Boolean = false
    override suspend fun goForward(): Boolean = false
    override suspend fun reload() = Unit
    override suspend fun performDownloadAction(id: DownloadId, action: DownloadAction) = false
    override suspend fun setFilteringEnabled(enabled: Boolean) = Unit
    override suspend fun setSiteFilteringException(
        host: String,
        allow: Boolean,
        plane: SiteFilteringPlane,
    ): Boolean = true
    override suspend fun flushFilteringCounts() = Unit
}

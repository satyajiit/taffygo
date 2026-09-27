// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import android.graphics.Bitmap
import com.taffygo.browser.ui.core.analytics.AnalyticsClient
import com.taffygo.browser.ui.core.analytics.AnalyticsEvent
import com.taffygo.browser.ui.core.browser.FilteringSettings
import com.taffygo.browser.ui.core.browser.FrequentSite
import com.taffygo.browser.ui.core.browser.FrequentSitesRepository
import com.taffygo.browser.ui.core.browser.NavigationState
import com.taffygo.browser.ui.core.browser.PageAppearance
import com.taffygo.browser.ui.core.browser.SiteFilteringPlane
import com.taffygo.browser.ui.core.browser.TabArtwork
import com.taffygo.browser.ui.core.browser.BrowserRepository
import com.taffygo.browser.ui.core.model.AddressBarInterpretation
import com.taffygo.browser.ui.core.model.BrowserNotice
import com.taffygo.browser.ui.core.model.DownloadAction
import com.taffygo.browser.ui.core.model.DownloadId
import com.taffygo.browser.ui.core.model.DownloadRecord
import com.taffygo.browser.ui.core.model.Suggestion
import com.taffygo.browser.ui.core.model.Tab
import com.taffygo.browser.ui.core.model.TabId
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyNavigator
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow

/**
 * @param pageHistory how many entries the selected tab can go back through.
 */
internal class FakeBrowser(
    private var pageHistory: Int = 0,
    tabs: List<Tab> = emptyList(),
    host: String = "docs.example.test",
    title: String = "Retention",
    canonicalUrl: String = host.takeIf(String::isNotBlank)?.let { "https://$it/" }.orEmpty(),
) : BrowserRepository {
    var dismissed = false
        private set

    var timesWentBack = 0
        private set

    var timesWentForward = 0
        private set

    var timesStoppedLoading = 0
        private set

    private val noticeState = MutableStateFlow<BrowserNotice?>(null)

    override val tabs: StateFlow<List<Tab>> = MutableStateFlow(tabs)
    override val navigation: StateFlow<NavigationState> =
        MutableStateFlow(
            NavigationState(host = host, title = title, canonicalUrl = canonicalUrl),
        )
    override val pageAppearance: StateFlow<PageAppearance> = MutableStateFlow(PageAppearance())
    override val downloads: StateFlow<List<DownloadRecord>> = MutableStateFlow(emptyList())
    override val tabArtwork: StateFlow<Map<TabId, TabArtwork>> = MutableStateFlow(emptyMap())
    override val siteMarks: StateFlow<Map<String, Bitmap>> = MutableStateFlow(emptyMap())
    override val notice: StateFlow<BrowserNotice?> = noticeState.asStateFlow()

    override suspend fun requestSiteMarks(hosts: Collection<String>) = Unit

    fun refuse(notice: BrowserNotice) {
        noticeState.value = notice
    }

    override fun resolve(input: String): AddressBarInterpretation =
        AddressBarInterpretation.GoTo(input, input)

    override fun suggestions(input: String): List<Suggestion> = emptyList()

    override suspend fun commit(interpretation: AddressBarInterpretation) = Unit

    override fun dismissNotice() {
        dismissed = true
        noticeState.value = null
    }

    override suspend fun selectTab(id: TabId) = Unit

    override suspend fun closeTab(id: TabId) = Unit

    override suspend fun openTab(host: String, isPrivate: Boolean): TabId = TabId("tab")

    override suspend fun goBack(): Boolean {
        timesWentBack++
        if (pageHistory == 0) return false
        pageHistory--
        return true
    }

    override suspend fun goForward(): Boolean {
        timesWentForward++
        return true
    }

    override suspend fun reload() = Unit

    override suspend fun stopLoading(): Boolean {
        timesStoppedLoading++
        return true
    }

    override suspend fun performDownloadAction(id: DownloadId, action: DownloadAction) = false

    override val filtering: StateFlow<FilteringSettings> = MutableStateFlow(FilteringSettings())

    override suspend fun setFilteringEnabled(enabled: Boolean) = Unit

    /** Every exception command this browser was given, in call order. */
    val siteExceptions = mutableListOf<Triple<String, Boolean, SiteFilteringPlane>>()

    /** What the seam answers. False is the browser refusing to record. */
    var recordsSiteExceptions = true

    override suspend fun setSiteFilteringException(
        host: String,
        allow: Boolean,
        plane: SiteFilteringPlane,
    ): Boolean {
        siteExceptions += Triple(host, allow, plane)
        return recordsSiteExceptions
    }

    override suspend fun flushFilteringCounts() = Unit
}

internal class NoNavigation : TaffyNavigator {
    override fun goTo(destination: TaffyDestination) = Unit

    override fun replaceCurrent(destination: TaffyDestination) = Unit

    override fun goBack(): Boolean = false

    override fun goHome() = Unit

    override fun restart(destination: TaffyDestination) = Unit

    override fun popWhile(shouldPop: (TaffyDestination) -> Boolean) = Unit
}

internal class RecordingNavigator : TaffyNavigator {
    var timesWentBack = 0
        private set

    var timesLeftToBackground = 0
        private set

    val visited = mutableListOf<TaffyDestination>()

    override fun goTo(destination: TaffyDestination) {
        visited += destination
    }

    override fun replaceCurrent(destination: TaffyDestination) = Unit

    override fun goBack(): Boolean {
        timesWentBack++
        return true
    }

    override fun leaveToBackground(): Boolean {
        timesLeftToBackground++
        return true
    }

    override fun goHome() = Unit

    override fun restart(destination: TaffyDestination) = Unit

    override fun popWhile(shouldPop: (TaffyDestination) -> Boolean) = Unit
}

internal class NoAnalytics : AnalyticsClient {
    override fun record(event: AnalyticsEvent) = Unit

    override fun recent(): List<AnalyticsEvent> = emptyList()
}

internal class NoFrequentSites : FrequentSitesRepository {
    override val sites: StateFlow<List<FrequentSite>> = MutableStateFlow(emptyList())

    override suspend fun recordVisit(host: String, title: String) = Unit
}

/** Test-only closed find seam; production has no fallback binding. */
internal class EmptyFindInPagePort : FindInPagePort {
    override val isAvailable: Boolean = false
    override val matches: StateFlow<FindInPagePort.MatchCount> =
        MutableStateFlow(FindInPagePort.MatchCount())

    override suspend fun find(query: String): FindInPagePort.MatchCount =
        FindInPagePort.MatchCount()

    override suspend fun next(): FindInPagePort.MatchCount = FindInPagePort.MatchCount()
    override suspend fun previous(): FindInPagePort.MatchCount = FindInPagePort.MatchCount()
    override suspend fun clear() = Unit
}

/** Test-only closed site seam; production has no fallback binding. */
internal class EmptySiteInfoRepository : SiteInfoRepository {
    override val desktopSiteAvailable: Boolean = false
    override val permissionRevision: StateFlow<Long> = MutableStateFlow(0L)
    override fun isDesktopSite(host: String): Boolean = false
    override fun permissionsFor(host: String) = SiteInfoRepository.PermissionState.Unavailable
    override suspend fun refreshPermissions(host: String) = Unit
    override suspend fun setDesktopSite(host: String, enabled: Boolean) = Unit
    override suspend fun resetPermissions(
        host: String,
    ) = SiteInfoRepository.PermissionResetResult.UNAVAILABLE
}

/** Test-only closed page-zoom seam; production has no fallback binding. */
internal class EmptyPageZoomRepository : PageZoomRepository {
    override val state: StateFlow<PageZoomState> = MutableStateFlow(PageZoomState())
    override suspend fun zoomIn() = Unit
    override suspend fun zoomOut() = Unit
    override suspend fun reset() = Unit
}

internal fun browserMainViewModel(
    browser: FakeBrowser = FakeBrowser(),
    find: FindInPagePort = EmptyFindInPagePort(),
    writer: BookmarksWriter = EmptyBookmarksWriter(),
    siteInfo: SiteInfoRepository = EmptySiteInfoRepository(),
    pageZoom: PageZoomRepository = EmptyPageZoomRepository(),
): BrowserMainViewModel = BrowserMainViewModel(
    browser,
    NoAnalytics(),
    FakeTaffyParts(),
    NoFrequentSites(),
    find,
    writer,
    siteInfo,
    pageZoom,
)

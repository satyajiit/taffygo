// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.taffygo.browser.ui.core.analytics.AnalyticsClient
import com.taffygo.browser.ui.core.analytics.AnalyticsEvent
import com.taffygo.browser.ui.core.assets.TaffyPartsRepository
import com.taffygo.browser.ui.core.browser.BrowserRepository
import com.taffygo.browser.ui.core.browser.FrequentSite
import com.taffygo.browser.ui.core.browser.FrequentSitesRepository
import com.taffygo.browser.ui.core.model.AddressBarInterpretation
import com.taffygo.browser.ui.core.model.TaffyPartId
import com.taffygo.browser.ui.core.model.TaffyPartProgress
import com.taffygo.browser.ui.core.model.TaffyPartsState
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyNavigator
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.flow.launchIn
import kotlinx.coroutines.flow.onEach
import kotlinx.coroutines.flow.runningFold
import kotlinx.coroutines.flow.stateIn
import kotlinx.coroutines.launch

/**
 * Screen SCR-101's one source of truth.
 *
 * Browser truth is derived from the repository's flows. The overflow and the
 * site sheet are held here; find in page and save page each keep their own
 * state and in-flight work in a controller this view model delegates to and
 * reads back through the projection.
 */
class BrowserMainViewModel(
    private val browser: BrowserRepository,
    private val analytics: AnalyticsClient,
    private val parts: TaffyPartsRepository,
    private val frequentSites: FrequentSitesRepository,
    private val findInPagePort: FindInPagePort,
    private val bookmarksWriter: BookmarksWriter,
    private val siteInfo: SiteInfoRepository,
    private val pageZoom: PageZoomRepository,
) : ViewModel() {

    private val moreOpen = MutableStateFlow(false)
    private val siteFilteringOpen = MutableStateFlow(false)
    private val findInPage = FindInPageController(findInPagePort, viewModelScope)
    private val savePage = SavePageController(browser, bookmarksWriter, viewModelScope)
    private val permissionReset = SitePermissionResetController(siteInfo)
    private val siteBlocking = SiteBlockingController(browser)

    private val liveProgress: StateFlow<Map<TaffyPartId, TaffyPartProgress>> = parts.progress
        .runningFold(emptyMap<TaffyPartId, TaffyPartProgress>()) { seen, report ->
            seen + (report.id to report)
        }
        .stateIn(
            scope = viewModelScope,
            started = SharingStarted.WhileSubscribed(SUBSCRIPTION_TIMEOUT_MILLIS),
            initialValue = emptyMap(),
        )

    init {
        frequentSites.sites
            .onEach { sites -> browser.requestSiteMarks(sites.map(FrequentSite::host)) }
            .launchIn(viewModelScope)
        browser.navigation
            .onEach { nav ->
                if (nav.host.isNotBlank()) browser.requestSiteMarks(listOf(nav.host))
            }
            .launchIn(viewModelScope)
    }

    val state: StateFlow<BrowserMainUiState> =
        combine(
            combine(
                browser.navigation,
                browser.tabs,
                combine(
                    moreOpen,
                    siteFilteringOpen,
                    findInPage.state,
                    savePage.open,
                    savePage.status,
                    ::BrowserMainChromeOverlay,
                ),
                browser.notice,
                browser.pageAppearance,
            ) { navigation, tabs, chrome, notice, appearance ->
                BrowserMainInputs(navigation, tabs, chrome, notice, appearance)
            },
            parts.state,
            liveProgress,
            frequentSites.sites,
            combine(
                browser.tabArtwork,
                browser.siteMarks,
                browser.filtering,
                pageZoom.state,
                // Three flows in one slot: `combine` takes at most five, and
                // the two sheet actions belong together anyway.
                combine(
                    siteInfo.permissionRevision,
                    permissionReset.state,
                    siteBlocking.state,
                ) { _, reset, blocking -> reset to blocking },
            ) { artwork, marks, filtering, zoom, actions ->
                BrowserMainResources(
                    artwork,
                    marks,
                    filtering,
                    zoom,
                    actions.first,
                    actions.second,
                )
            },
        ) { inputs, partsState, progress, sites, resources ->
            projectFrom(inputs, partsState, progress, sites, resources)
        }
            .stateIn(
                scope = viewModelScope,
                started = SharingStarted.WhileSubscribed(SUBSCRIPTION_TIMEOUT_MILLIS),
                initialValue = projectFrom(
                    BrowserMainInputs(
                        browser.navigation.value,
                        browser.tabs.value,
                        BrowserMainChromeOverlay(
                            moreOpen.value,
                            siteFilteringOpen.value,
                            findInPage.state.value,
                            savePage.open.value,
                            savePage.status.value,
                        ),
                        browser.notice.value,
                        browser.pageAppearance.value,
                    ),
                    parts.state.value,
                    liveProgress.value,
                    frequentSites.sites.value,
                    BrowserMainResources(
                        browser.tabArtwork.value,
                        browser.siteMarks.value,
                        browser.filtering.value,
                        pageZoom.state.value,
                        permissionReset.state.value,
                        siteBlocking.state.value,
                    ),
                ),
            )

    /** Act on something the user did. */
    fun onIntent(intent: BrowserMainIntent, navigator: TaffyNavigator) {
        when (intent) {
            BrowserMainIntent.FocusAddressBar -> {
                if (state.value.content == BrowserContent.PREPARING) return
                navigator.goTo(TaffyDestination.AddressBar)
            }
            BrowserMainIntent.OpenTabSwitcher -> navigator.goTo(TaffyDestination.TabSwitcher)
            BrowserMainIntent.OpenDownloads -> goTo(navigator, TaffyDestination.Downloads)
            BrowserMainIntent.OpenSettings -> goTo(navigator, TaffyDestination.SettingsHome)
            BrowserMainIntent.OpenWorkspaces -> goTo(navigator, TaffyDestination.WorkspaceList)
            BrowserMainIntent.GoBack -> viewModelScope.launch { browser.goBack() }
            BrowserMainIntent.GoForward -> viewModelScope.launch { browser.goForward() }
            BrowserMainIntent.SystemBack -> handleSystemBack(navigator)
            is BrowserMainIntent.OpenSite -> {
                if (state.value.content == BrowserContent.PREPARING) return
                viewModelScope.launch {
                    browser.commit(AddressBarInterpretation.GoTo(intent.host, intent.host))
                }
            }
            BrowserMainIntent.RetryPageTools -> viewModelScope.launch {
                val id = state.value.startPageGate.partId
                if (id == null) parts.retryCore() else parts.request(id)
            }
            BrowserMainIntent.Reload -> {
                moreOpen.value = false
                viewModelScope.launch { browser.reload() }
            }
            BrowserMainIntent.StopLoading -> {
                moreOpen.value = false
                viewModelScope.launch { browser.stopLoading() }
            }
            BrowserMainIntent.OpenMore -> moreOpen.value = true
            BrowserMainIntent.DismissMore -> moreOpen.value = false
            BrowserMainIntent.DismissNotice -> browser.dismissNotice()
            BrowserMainIntent.OpenSiteFiltering -> {
                moreOpen.value = false
                permissionReset.clear()
                siteBlocking.clear()
                siteFilteringOpen.value = true
                val host = state.value.siteFiltering.host
                viewModelScope.launch {
                    browser.flushFilteringCounts()
                    siteInfo.refreshPermissions(host)
                }
            }
            BrowserMainIntent.DismissSiteFiltering -> closeSiteFiltering()
            is BrowserMainIntent.SetSiteBlocking -> {
                val host = state.value.siteFiltering.host
                viewModelScope.launch {
                    siteBlocking.set(host, intent.blocked) {
                        state.value.siteFiltering.host == host
                    }
                }
            }
            BrowserMainIntent.OpenFilteringSettings -> {
                closeSiteFiltering()
                navigator.goTo(TaffyDestination.AdAndTrackerBlocking)
            }
            BrowserMainIntent.OpenHistory -> goTo(navigator, TaffyDestination.History)
            BrowserMainIntent.OpenBookmarks -> goTo(navigator, TaffyDestination.Bookmarks)
            BrowserMainIntent.OpenLibrary -> goTo(navigator, TaffyDestination.LibraryHome)
            BrowserMainIntent.OpenYou -> goTo(navigator, TaffyDestination.You)
            BrowserMainIntent.OpenFindInPage -> {
                moreOpen.value = false
                findInPage.open()
                analytics.record(AnalyticsEvent.ScreenShown(FIND_SCREEN_ID))
            }
            BrowserMainIntent.DismissFindInPage -> findInPage.close()
            is BrowserMainIntent.FindQueryChanged ->
                findInPage.apply(FindInPageIntent.QueryChanged(intent.query))
            BrowserMainIntent.FindNext -> findInPage.apply(FindInPageIntent.Next)
            BrowserMainIntent.FindPrevious -> findInPage.apply(FindInPageIntent.Previous)
            BrowserMainIntent.SharePage, BrowserMainIntent.OpenSavedFlows -> moreOpen.value = false
            // Named and deliberately not acted on here. Taking over is about
            // the task, and this view model holds the page;
            // `BrowserTakeoverViewModel` is the one with a task to stop, and
            // the screen routes the intent to it exactly as it routes sharing
            // to the platform.
            BrowserMainIntent.TakeOver -> Unit
            BrowserMainIntent.OpenSavePage -> {
                moreOpen.value = false
                if (!state.value.hasBeenNowhere) {
                    savePage.begin()
                    analytics.record(AnalyticsEvent.ScreenShown(SAVE_SCREEN_ID))
                }
            }
            BrowserMainIntent.DismissSavePage -> savePage.close()
            BrowserMainIntent.ConfirmSavePage -> savePage.confirm(state.value.savePage)
            is BrowserMainIntent.ChooseSaveFolder -> Unit
            BrowserMainIntent.ToggleDesktopSite -> viewModelScope.launch {
                val sheet = state.value.siteFiltering
                if (sheet.desktopSiteAvailable) {
                    siteInfo.setDesktopSite(sheet.host, !sheet.desktopSite)
                }
            }
            BrowserMainIntent.RequestSitePermissionReset -> requestSitePermissionReset()
            BrowserMainIntent.ConfirmSitePermissionReset -> confirmSitePermissionReset()
            BrowserMainIntent.DismissSitePermissionReset -> dismissSitePermissionReset()
            BrowserMainIntent.ZoomPageIn -> viewModelScope.launch { pageZoom.zoomIn() }
            BrowserMainIntent.ZoomPageOut -> viewModelScope.launch { pageZoom.zoomOut() }
            BrowserMainIntent.ResetPageZoom -> viewModelScope.launch { pageZoom.reset() }
        }
    }

    fun onShown() {
        analytics.record(AnalyticsEvent.ScreenShown(TaffyDestination.BrowserMain.screenId))
    }

    private fun goTo(navigator: TaffyNavigator, destination: TaffyDestination) {
        moreOpen.value = false
        navigator.goTo(destination)
    }

    private fun handleSystemBack(navigator: TaffyNavigator) {
        when {
            moreOpen.value -> moreOpen.value = false
            state.value.siteFiltering.permissionResetConfirmation ->
                dismissSitePermissionReset()
            siteFilteringOpen.value -> closeSiteFiltering()
            findInPage.state.value.open -> findInPage.close()
            savePage.open.value -> savePage.close()
            else -> viewModelScope.launch {
                if (!browser.goBack()) navigator.leaveToBackground()
            }
        }
    }

    private fun confirmSitePermissionReset() {
        val sheet = state.value.siteFiltering
        if (!sheet.permissionResetConfirmation || sheet.host.isBlank()) return
        val host = sheet.host
        viewModelScope.launch {
            permissionReset.confirm(host) {
                siteFilteringOpen.value && state.value.siteFiltering.host == host
            }
        }
    }

    private fun requestSitePermissionReset() {
        val sheet = state.value.siteFiltering
        if (!sheet.canResetPermissions || sheet.host.isBlank()) return
        permissionReset.request(sheet.host)
    }

    private fun dismissSitePermissionReset() {
        if (!state.value.siteFiltering.permissionResetConfirmation) return
        permissionReset.clear()
    }

    private fun closeSiteFiltering() {
        siteFilteringOpen.value = false
        permissionReset.clear()
    }

    private fun projectFrom(
        inputs: BrowserMainInputs,
        partsState: TaffyPartsState,
        progress: Map<TaffyPartId, TaffyPartProgress>,
        sites: List<FrequentSite>,
        resources: BrowserMainResources,
    ): BrowserMainUiState = projectBrowserMain(
        inputs.navigation,
        inputs.tabs,
        inputs.chrome.moreOpen,
        inputs.notice,
        inputs.appearance,
        startPageGate(partsState, progress),
        sites,
        resources.artwork,
        resources.marks,
        resources.filtering,
        inputs.chrome.siteFilteringOpen,
        inputs.chrome.findInPage,
        inputs.chrome.savePageOpen,
        bookmarksWriter.isAvailable,
        inputs.chrome.savePageStatus,
        siteInfo.isDesktopSite(inputs.navigation.host),
        siteInfo.desktopSiteAvailable,
        siteInfo.permissionsFor(inputs.navigation.host),
        resources.permissionReset.status.takeIf {
            resources.permissionReset.host == inputs.navigation.host
        } ?: SiteFilteringUiState.ActionProgress.IDLE,
        resources.permissionReset.host.isNotBlank() &&
            resources.permissionReset.host == inputs.navigation.host &&
            resources.permissionReset.status == SiteFilteringUiState.ActionProgress.IDLE,
        resources.siteBlocking.status.takeIf {
            resources.siteBlocking.host == inputs.navigation.host
        } ?: SiteFilteringUiState.ActionProgress.IDLE,
        resources.pageZoom,
    )

    private companion object {
        const val SUBSCRIPTION_TIMEOUT_MILLIS = 5_000L
        const val FIND_SCREEN_ID = "SCR-106"
        const val SAVE_SCREEN_ID = "SCR-811"
    }
}

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
import com.taffygo.browser.ui.core.model.TaffyPartId
import com.taffygo.browser.ui.core.model.TaffyPartProgress
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyNavigator
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.flow.launchIn
import kotlinx.coroutines.flow.onEach
import kotlinx.coroutines.flow.runningFold
import kotlinx.coroutines.flow.stateIn
import kotlinx.coroutines.launch

/**
 * Screen SCR-102's one source of truth.
 *
 * ## Every way off this screen opens a tab first
 *
 * SCR-102 is not a tab. It is the chooser in front of one: the person has said
 * "new tab" and has not yet said where. So each of the ways forward opens
 * the tab and then decides what goes in it, and one of them used not to.
 *
 * [NewTabIntent.ComposerFocused] used to be a navigation and nothing else, and
 * from that point nothing in the application remembered that a new tab had been
 * asked for. The box commits through `BrowserRepository.commit`, which
 * navigates the *selected* tab, so typing an address here replaced the page the
 * person was already on and the tab count never moved. Opening the tab on focus
 * is what keeps the request alive: the composer then commits into the tab this
 * screen made, exactly as [NewTabIntent.OpenSite] commits into the tab it
 * makes. The box no longer changes screens to be typed into, and that changed
 * nothing about this — the tab is opened at the same moment it always was.
 *
 * The cost is a tab on the start page if the person taps the box and types
 * nothing, which is the same tab a browser leaves behind when its new-tab
 * button is pressed and then abandoned. That is a browser with an extra empty
 * tab; the alternative was a browser that cannot open a second one.
 */
class NewTabViewModel(
    private val browser: BrowserRepository,
    private val analytics: AnalyticsClient,
    private val parts: TaffyPartsRepository,
    private val frequentSites: FrequentSitesRepository,
) : ViewModel() {

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
        // The store keeps hosts, not marks, so each host's mark is asked of
        // the browser's local favicon store as the ranking (re)arrives. For
        // the view model's whole life rather than while subscribed: the ask
        // is idempotent, and the answers are waiting before the first frame.
        frequentSites.sites
            .onEach { sites -> browser.requestSiteMarks(sites.map(FrequentSite::host)) }
            .launchIn(viewModelScope)
    }

    /** What screen SCR-102 renders. */
    val state: StateFlow<NewTabUiState> =
        combine(
            browser.tabs,
            parts.state,
            liveProgress,
            frequentSites.sites,
            // Nested because `combine` is typed up to five flows, and these
            // two are one subject: the engine bitmaps a tile may borrow.
            combine(browser.tabArtwork, browser.siteMarks, ::Pair),
        ) { tabs, partsState, progress, sites, (artwork, marks) ->
            projectNewTab(tabs, startPageGate(partsState, progress), sites, artwork, marks)
        }
            .stateIn(
                scope = viewModelScope,
                started = SharingStarted.WhileSubscribed(SUBSCRIPTION_TIMEOUT_MILLIS),
                initialValue = projectNewTab(
                    browser.tabs.value,
                    startPageGate(parts.state.value),
                    frequentSites.sites.value,
                    browser.tabArtwork.value,
                    browser.siteMarks.value,
                ),
            )

    /** Act on something the user did. */
    fun onIntent(intent: NewTabIntent, navigator: TaffyNavigator) {
        when (intent) {
            // The tab first, then the words that say what goes in it. See this
            // class's own documentation: without the tab, the box has nothing
            // of its own to commit into and takes over the current page. It
            // navigates nowhere, because the box the person is typing into is
            // already on the screen in front of them.
            NewTabIntent.ComposerFocused -> {
                if (!state.value.startPageGate.ready) return
                viewModelScope.launch { browser.openTab(NOWHERE_YET) }
            }
            // The chooser is finished once the tab is open, so it is replaced
            // rather than left underneath — and with it every browsing chrome
            // surface between here and the page. See `BackStack.replaceCurrent`:
            // pushing the browsing surface back on is what made the system back
            // button walk through chrome the person had already left.
            is NewTabIntent.OpenSite -> {
                if (!state.value.startPageGate.ready) return
                viewModelScope.launch {
                    browser.openTab(intent.host)
                    navigator.replaceCurrent(TaffyDestination.BrowserMain)
                }
            }
            // Two different asks behind one control, because the person is
            // looking at one wait. A named row is asked for again; a browser
            // that has described no rows at all is asked to start its core,
            // which is the only move left when the list is empty and is the
            // one the screen used to have no way of making.
            NewTabIntent.RetryPageTools -> viewModelScope.launch {
                val id = state.value.startPageGate.partId
                if (id == null) parts.retryCore() else parts.request(id)
            }
            // Pushed rather than replaced, and no tab opened. These four leave
            // the chooser standing so back returns to it: the person has not
            // answered "where does this tab go" yet, and coming back to a
            // screen that had been replaced would mean losing the question.
            NewTabIntent.OpenTabSwitcher -> navigator.goTo(TaffyDestination.TabSwitcher)
            NewTabIntent.OpenDownloads -> navigator.goTo(TaffyDestination.Downloads)
            NewTabIntent.OpenWorkspaces -> navigator.goTo(TaffyDestination.WorkspaceList)
            NewTabIntent.OpenSettings -> navigator.goTo(TaffyDestination.SettingsHome)
            // The plus's one destination, on the same rule as the row's four above.
            NewTabIntent.OpenLibrary -> navigator.goTo(TaffyDestination.LibraryHome)
        }
    }

    /** Record that this screen was shown. */
    fun onShown() {
        analytics.record(AnalyticsEvent.ScreenShown(TaffyDestination.NewTab.screenId))
    }

    private companion object {
        const val SUBSCRIPTION_TIMEOUT_MILLIS = 5_000L

        /**
         * A tab opened on no address at all.
         *
         * A blank host is the seam's own word for "the browser's start page":
         * `ChromiumBrowserMediator.addressFor` reads it that way and opens the
         * profile's configured start page. No placeholder host or network
         * destination is invented by the UI.
         */
        const val NOWHERE_YET = ""
    }
}

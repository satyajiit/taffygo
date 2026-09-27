// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import android.graphics.Bitmap
import com.taffygo.browser.ui.core.analytics.AnalyticsClient
import com.taffygo.browser.ui.core.analytics.AnalyticsEvent
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
import com.taffygo.browser.ui.core.model.TaskTemplate
import com.taffygo.browser.ui.core.task.TaskProjection
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyNavigator
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import taffy.core_api.TaskPhase

/**
 * The stand-ins the box's start tests share: a browser that resolves the way
 * a test says and whose tabs a test can move, a navigator that records, and
 * an analytics client that keeps nothing.
 */
internal class StartTestBrowser(
    tabs: List<Tab> = emptyList(),
    private val resolve: (String) -> AddressBarInterpretation,
) : BrowserRepository {
    val committed = mutableListOf<String>()
    private val tabsFlow = MutableStateFlow(tabs)
    override val tabs: StateFlow<List<Tab>> = tabsFlow
    override val navigation = MutableStateFlow(NavigationState(host = "", title = ""))
    override val pageAppearance: StateFlow<PageAppearance> = MutableStateFlow(PageAppearance())
    override val downloads: StateFlow<List<DownloadRecord>> = MutableStateFlow(emptyList())
    override val tabArtwork: StateFlow<Map<TabId, TabArtwork>> = MutableStateFlow(emptyMap())
    override val siteMarks: StateFlow<Map<String, Bitmap>> = MutableStateFlow(emptyMap())
    override val notice: StateFlow<BrowserNotice?> = MutableStateFlow(null)
    override val filtering: StateFlow<FilteringSettings> = MutableStateFlow(FilteringSettings())

    /** The tabs as the browser now holds them: a test closing or opening one. */
    fun tabsBecome(tabs: List<Tab>) {
        tabsFlow.value = tabs
    }

    override fun resolve(input: String): AddressBarInterpretation = resolve.invoke(input)
    override fun suggestions(input: String): List<Suggestion> = emptyList()
    override suspend fun commit(interpretation: AddressBarInterpretation) {
        committed += interpretation.input
    }
    override fun dismissNotice() = Unit
    override suspend fun selectTab(id: TabId) = Unit
    override suspend fun closeTab(id: TabId) = Unit
    override suspend fun openTab(host: String, isPrivate: Boolean) = TabId("opened")
    override suspend fun goBack() = false
    override suspend fun goForward() = false
    override suspend fun reload() = Unit
    override suspend fun performDownloadAction(id: DownloadId, action: DownloadAction) = false
    override suspend fun requestSiteMarks(hosts: Collection<String>) = Unit
    override suspend fun setFilteringEnabled(enabled: Boolean) = Unit
    override suspend fun setSiteFilteringException(
        host: String,
        allow: Boolean,
        plane: SiteFilteringPlane,
    ): Boolean = true
    override suspend fun flushFilteringCounts() = Unit
}

internal class RecordingNavigation : TaffyNavigator {
    val visited = mutableListOf<TaffyDestination>()
    val replaced = mutableListOf<TaffyDestination>()
    var backs = 0
        private set
    override fun goTo(destination: TaffyDestination) {
        visited += destination
    }
    override fun replaceCurrent(destination: TaffyDestination) {
        replaced += destination
    }
    override fun goBack(): Boolean {
        backs++
        return true
    }
    override fun goHome() = Unit
    override fun restart(destination: TaffyDestination) = Unit
    override fun popWhile(shouldPop: (TaffyDestination) -> Boolean) = Unit
}

internal data object NoEvents : AnalyticsClient {
    override fun record(event: AnalyticsEvent) = Unit
    override fun recent(): List<AnalyticsEvent> = emptyList()
}

/** A task as the core would first publish it, under the goal it was started with. */
internal fun startedTask(
    id: String,
    goal: String,
    template: TaskTemplate = TaskTemplate.WEB_ERRAND,
    phase: TaskPhase = TaskPhase.PLANNING,
): TaskProjection = TaskProjection(
    id = id,
    revision = 1u,
    phase = phase,
    goal = goal,
    template = template,
    progressBasisPoints = 0u,
    statusMessageKey = "task.planning",
    failure = null,
    pendingAction = null,
)

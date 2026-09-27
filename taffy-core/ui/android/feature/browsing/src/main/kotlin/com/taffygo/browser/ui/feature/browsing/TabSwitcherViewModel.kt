// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import android.graphics.Bitmap
import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.taffygo.browser.ui.core.analytics.AnalyticsClient
import com.taffygo.browser.ui.core.analytics.AnalyticsEvent
import com.taffygo.browser.ui.core.browser.BrowserRepository
import com.taffygo.browser.ui.core.browser.TabArtwork
import com.taffygo.browser.ui.core.task.TaskRepository
import com.taffygo.browser.ui.core.task.TaskRepositoryState
import com.taffygo.browser.ui.core.model.Tab
import com.taffygo.browser.ui.core.model.TabId
import com.taffygo.browser.ui.core.model.TaskDisplayState
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyNavigator
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.flow.launchIn
import kotlinx.coroutines.flow.onEach
import kotlinx.coroutines.flow.stateIn
import kotlinx.coroutines.launch

/**
 * Screen SCR-104's one source of truth.
 *
 * It reads the task and never commands it. The fact count on an amber card is
 * the task's own record of what it took from that host, and there is nowhere
 * else it exists; reading it here keeps this feature's rule intact — it starts
 * no task and changes none.
 */
class TabSwitcherViewModel(
    private val browser: BrowserRepository,
    private val tasks: TaskRepository,
    private val analytics: AnalyticsClient,
) : ViewModel() {

    private val group = MutableStateFlow(TabSwitcherGroup.YOURS)
    private val taffyGroupExpanded = MutableStateFlow(false)
    private val closeVisibleConfirmation = MutableStateFlow(false)
    private val searchQuery = MutableStateFlow("")
    private val selectedForAsk = MutableStateFlow(emptySet<TabId>())
    private val isSelecting = MutableStateFlow(false)

    init {
        // Cards borrow the same local favicon store the start page does, so
        // a tab whose in-memory mark was never constructed still has a face.
        browser.tabs
            .onEach { tabs ->
                browser.requestSiteMarks(tabs.mapNotNull { tab -> tab.host.takeIf(String::isNotBlank) })
            }
            .launchIn(viewModelScope)
    }

    /** What screen SCR-104 renders. */
    val state: StateFlow<TabSwitcherUiState> =
        combine(
            browser.tabs,
            tasks.state,
            combine(browser.tabArtwork, browser.siteMarks, ::Pair),
            combine(group, taffyGroupExpanded, closeVisibleConfirmation, searchQuery, ::Chrome),
            combine(selectedForAsk, isSelecting, ::Pair),
        ) { tabs, task, (artwork, marks), chrome, selection ->
            project(tabs, task, artwork, marks, chrome, selection)
        }
            .stateIn(
                scope = viewModelScope,
                started = SharingStarted.WhileSubscribed(SUBSCRIPTION_TIMEOUT_MILLIS),
                initialValue = project(
                    browser.tabs.value,
                    tasks.state.value,
                    browser.tabArtwork.value,
                    browser.siteMarks.value,
                    Chrome(
                        group.value,
                        taffyGroupExpanded.value,
                        closeVisibleConfirmation.value,
                        searchQuery.value,
                    ),
                    selectedForAsk.value to isSelecting.value,
                ),
            )

    /** Act on something the user did. */
    fun onIntent(intent: TabSwitcherIntent, navigator: TaffyNavigator) {
        when (intent) {
            is TabSwitcherIntent.Select -> {
                if (isSelecting.value && selectedForAsk.value.isNotEmpty()) {
                    toggleSelected(intent.id)
                    return
                }
                viewModelScope.launch {
                    browser.selectTab(intent.id)
                    navigator.replaceCurrent(TaffyDestination.BrowserMain)
                }
            }
            is TabSwitcherIntent.Close -> viewModelScope.launch { browser.closeTab(intent.id) }
            is TabSwitcherIntent.SelectGroup -> group.value = intent.group
            TabSwitcherIntent.ToggleTaffyGroup ->
                taffyGroupExpanded.value = !taffyGroupExpanded.value
            // The kind of tab this opens is the segment it was pressed in.
            // It opened an ordinary tab from inside the Private segment until
            // now, which was both surprising there and the reason the start
            // page had to carry a private-tab card of its own: this was the
            // obvious way in and it did not work, so a second one was built
            // somewhere else. One way in, where the private tabs already are.
            TabSwitcherIntent.NewTab -> viewModelScope.launch {
                browser.openTab("", isPrivate = group.value == TabSwitcherGroup.PRIVATE)
                navigator.replaceCurrent(TaffyDestination.BrowserMain)
            }
            // Carry exactly the tabs the row announced. The person's request
            // decides the shape; forcing a one-page table would refuse the
            // ordinary case where the promised tabs are on different sites.
            TabSwitcherIntent.StartWorkspace -> {
                val ids = state.value.workspaceTabIds
                if (ids.isEmpty()) return
                navigator.goTo(
                    TaffyDestination.AssistantBar(attachedTabIds = ids.map { it.value }),
                )
            }
            is TabSwitcherIntent.Search -> searchQuery.value = intent.query
            TabSwitcherIntent.RequestCloseVisible -> closeVisibleConfirmation.value = true
            TabSwitcherIntent.DismissCloseVisible -> closeVisibleConfirmation.value = false
            TabSwitcherIntent.ConfirmCloseVisible -> viewModelScope.launch {
                closeVisibleConfirmation.value = false
                state.value.closeVisibleTabs.forEach { browser.closeTab(it.id) }
            }
            is TabSwitcherIntent.LongPress -> beginSelection(intent.id)
            is TabSwitcherIntent.ToggleSelected -> toggleSelected(intent.id)
            TabSwitcherIntent.ClearSelection -> {
                isSelecting.value = false
                selectedForAsk.value = emptySet()
            }
            // The picked tabs go to the Ask sheet attached, which already has
            // the chips, the consent and the start: there is nothing a screen
            // in between would add.
            TabSwitcherIntent.AskTaffy -> {
                val ids = state.value.askTaffyTabIds
                if (ids.isEmpty()) return
                navigator.goTo(
                    TaffyDestination.AssistantBar(attachedTabIds = ids.map { it.value }),
                )
            }
        }
    }

    /** Record that this screen was shown. */
    fun onShown() {
        analytics.record(AnalyticsEvent.ScreenShown(TaffyDestination.TabSwitcher.screenId))
    }

    private fun beginSelection(id: TabId) {
        val tab = browser.tabs.value.firstOrNull { it.id == id } ?: return
        if (tab.isTaffyTab || tab.isPrivate || tab.hasBeenNowhere || tab.host.isBlank()) return
        isSelecting.value = true
        selectedForAsk.value = selectedForAsk.value + id
    }

    private fun toggleSelected(id: TabId) {
        val tab = browser.tabs.value.firstOrNull { it.id == id } ?: return
        if (tab.isTaffyTab || tab.isPrivate || tab.hasBeenNowhere || tab.host.isBlank()) return
        val next = if (id in selectedForAsk.value) {
            selectedForAsk.value - id
        } else {
            selectedForAsk.value + id
        }
        selectedForAsk.value = next
        isSelecting.value = next.isNotEmpty()
    }

    private fun project(
        tabs: List<Tab>,
        task: TaskRepositoryState,
        artwork: Map<TabId, TabArtwork>,
        siteMarks: Map<String, Bitmap>,
        chrome: Chrome,
        selection: Pair<Set<TabId>, Boolean>,
    ): TabSwitcherUiState = projectTabSwitcher(
        tabs = tabs,
        sources = emptyList(),
        taskIsRunning = task.task?.displayState == TaskDisplayState.RUNNING,
        group = chrome.group,
        taffyGroupExpanded = chrome.taffyGroupExpanded,
        closeVisibleConfirmationVisible = chrome.closeVisible,
        searchQuery = chrome.search,
        selectedForAsk = selection.first,
        isSelecting = selection.second,
        artwork = artwork,
        siteMarks = siteMarks,
        tabHoldingTaskIds = task.tabHoldingTaskIds(),
    )

    private data class Chrome(
        val group: TabSwitcherGroup,
        val taffyGroupExpanded: Boolean,
        val closeVisible: Boolean,
        val search: String,
    )

    private companion object {
        const val SUBSCRIPTION_TIMEOUT_MILLIS = 5_000L
    }
}

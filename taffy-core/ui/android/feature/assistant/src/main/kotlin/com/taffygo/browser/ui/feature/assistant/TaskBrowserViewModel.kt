// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.assistant

import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.taffygo.browser.ui.core.browser.BrowserRepository
import com.taffygo.browser.ui.core.model.TabId
import com.taffygo.browser.ui.core.task.TaskRepository
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyNavigator
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.flow.mapLatest
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.flow.distinctUntilChanged
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.flow.stateIn
import kotlinx.coroutines.launch

/** Projects tab ownership independently of the task's output and controls. */
class TaskBrowserViewModel(
    private val browser: BrowserRepository,
    private val tasks: TaskRepository,
) : ViewModel() {
    @OptIn(ExperimentalCoroutinesApi::class)
    private val taskPages = combine(
        tasks.state.map { it.task?.let { task -> task.id to task.revision } }.distinctUntilChanged(),
        browser.tabs,
    ) { task, tabs -> task to tabs }.mapLatest { (task, tabs) ->
        val taskId = task?.first
        projectTaskBrowser(
            taskId, tabs, emptyMap(),
            acceptedSources = taskId?.let { browser.tabsForTask(it) }.orEmpty(),
        )
    }

    /**
     * The hosts this task cited, asked of the profile's own favicon store.
     *
     * Asked rather than assumed: the store answers only for hosts it already
     * has a mark for, and a task's sources are frequently pages with no open
     * tab, so the tab artwork above cannot supply them. Requested on every
     * change of the source set and no oftener — the marks then arrive on
     * `browser.siteMarks` and are combined in below.
     */
    private val citedHosts = tasks.state
        .map { state -> state.task?.sources.orEmpty().map { it.host }.filter { it.isNotBlank() }.toSet() }
        .distinctUntilChanged()

    val state: StateFlow<TaskBrowserUiState> = combine(
        taskPages,
        browser.tabArtwork,
        browser.siteMarks,
        citedHosts,
    ) { pages, artwork, marks, hosts ->
        pages.copy(
            artwork = buildMap {
                pages.tabs.forEach { tab -> artwork[tab.id]?.let { put(tab.id, it) } }
            },
            siteMarks = buildMap {
                hosts.forEach { host -> marks[host]?.let { put(host, it) } }
            },
        )
    }.stateIn(
        scope = viewModelScope,
        started = SharingStarted.WhileSubscribed(5_000L),
        initialValue = projectTaskBrowser(tasks.state.value.task?.id, browser.tabs.value, browser.tabArtwork.value),
    )

    init {
        // One ask per distinct source set. The store answers from disk, so a
        // host it has never seen simply never appears and the row falls back to
        // its number rather than waiting on anything.
        viewModelScope.launch {
            citedHosts.collect { hosts -> if (hosts.isNotEmpty()) browser.requestSiteMarks(hosts) }
        }
    }

    /** A stale card cannot switch into a tab that has since left this task. */
    fun openTab(taskId: String?, tabId: TabId, navigator: TaffyNavigator) {
        if (taskId == null) return
        viewModelScope.launch {
            if (tasks.state.value.task?.id != taskId) return@launch
            if (state.value.taskId != taskId || state.value.tabs.none { it.id == tabId }) return@launch
            if (browser.tabs.value.none { it.id == tabId && !it.isPrivate }) return@launch
            if (browser.selectTaskTab(tabId, taskId)) {
                navigator.goTo(TaffyDestination.BrowserMain)
            }
        }
    }
}

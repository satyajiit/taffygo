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
import com.taffygo.browser.ui.core.browser.BrowserRepository
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
import com.taffygo.browser.ui.core.common.TaffyResult
import com.taffygo.browser.ui.core.task.CoreUiAvailability
import com.taffygo.browser.ui.core.task.TaskConsentIntent
import com.taffygo.browser.ui.core.task.TaskRepository
import com.taffygo.browser.ui.core.task.TaskRepositoryState
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyNavigator
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.resetMain
import kotlinx.coroutines.test.runCurrent
import kotlinx.coroutines.test.runTest
import kotlinx.coroutines.test.setMain
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Before
import org.junit.Test
import taffy.core_api.TaskPhase

/**
 * Screen SCR-104's way off the grid onto a tab.
 *
 * Selecting a tab already replaced the grid. Creating one used to *push* the
 * start page on top of it, so back from a new tab opened the grid again. A
 * browser's back stack is the pages.
 */
class TabSwitcherViewModelTest {

    private val dispatcher = StandardTestDispatcher()

    @Before
    fun setUp() {
        Dispatchers.setMain(dispatcher)
    }

    @After
    fun tearDown() {
        Dispatchers.resetMain()
    }

    @Test
    fun `a new tab opens on nothing and leaves the grid behind`() = runTest(dispatcher) {
        val browser = RecordingBrowser()
        val navigator = RecordingNavigator()
        val viewModel = TabSwitcherViewModel(browser, IdleTasks(), NoAnalytics())

        viewModel.onIntent(TabSwitcherIntent.NewTab, navigator)
        runCurrent()

        assertEquals(listOf("" to false), browser.opened)
        assertEquals(listOf(TaffyDestination.BrowserMain), navigator.replaced)
        assertEquals(emptyList<TaffyDestination>(), navigator.pushed)
    }

    /**
     * The add control makes the kind of tab the segment it was pressed in is
     * for.
     *
     * It made an ordinary tab from inside the Private segment until now, which
     * is why the start page had to carry a private-tab card of its own: the
     * obvious way in did not work, so a second one was built somewhere else.
     * The card is gone and this is the way in, so this is the assertion that
     * keeps private browsing reachable at all.
     */
    @Test
    fun `the add control in the private segment opens a private tab`() = runTest(dispatcher) {
        val browser = RecordingBrowser()
        val navigator = RecordingNavigator()
        val viewModel = TabSwitcherViewModel(browser, IdleTasks(), NoAnalytics())

        viewModel.onIntent(TabSwitcherIntent.SelectGroup(TabSwitcherGroup.PRIVATE), navigator)
        viewModel.onIntent(TabSwitcherIntent.NewTab, navigator)
        runCurrent()

        assertEquals(listOf("" to true), browser.opened)
        assertEquals(listOf(TaffyDestination.BrowserMain), navigator.replaced)
    }

    @Test
    fun `choosing a tab also leaves the grid behind`() = runTest(dispatcher) {
        val browser = RecordingBrowser()
        val navigator = RecordingNavigator()
        val viewModel = TabSwitcherViewModel(browser, IdleTasks(), NoAnalytics())

        viewModel.onIntent(TabSwitcherIntent.Select(TabId("tab_1")), navigator)
        runCurrent()

        assertEquals(listOf(TabId("tab_1")), browser.selected)
        assertEquals(listOf(TaffyDestination.BrowserMain), navigator.replaced)
    }

    @Test
    fun `Ask Taffy opens the sheet with the offered tabs attached`() = runTest(dispatcher) {
        val browser = RecordingBrowser(
            initial = listOf(
                Tab(TabId("tab_1"), "Retention policy", "docs.example.test", isSelected = true),
                Tab(TabId("tab_2"), "Product listing", "shop.example.test"),
            ),
        )
        val navigator = RecordingNavigator()
        val viewModel = TabSwitcherViewModel(browser, IdleTasks(), NoAnalytics())

        viewModel.onIntent(TabSwitcherIntent.AskTaffy, navigator)

        val destination = navigator.pushed.single() as TaffyDestination.AssistantBar
        assertEquals(listOf("tab_1"), destination.attachedTabIds)
        assertEquals("tab_1", destination.arguments[TaffyDestination.ATTACHED_TAB_IDS])
        assertFalse(destination.route.contains("tab_1"))
        assertEquals(emptyList<TaffyDestination>(), navigator.replaced)
    }

    @Test
    fun `new workspace carries exactly the eligible tabs its label announces`() = runTest(dispatcher) {
        val browser = RecordingBrowser(initial = listOf(
            Tab(TabId("orchard"), "Orchard", "127.0.0.1", isSelected = true),
            Tab(TabId("harbor"), "Harbor", "localhost"),
            Tab(TabId("another"), "Another listing", "localhost"),
            Tab(TabId("private"), "Private", "private.example", isPrivate = true),
            Tab(TabId("taffy"), "Task page", "task.example", isTaffyTab = true),
            Tab(TabId("blank"), "", "", hasBeenNowhere = true),
        ))
        val navigator = RecordingNavigator()
        val viewModel = TabSwitcherViewModel(browser, IdleTasks(), NoAnalytics())
        val announced = viewModel.state.value.workspaceTabCount

        viewModel.onIntent(TabSwitcherIntent.StartWorkspace, navigator)

        val destination = navigator.pushed.single() as TaffyDestination.AssistantBar
        assertEquals(listOf("orchard", "harbor", "another"), destination.attachedTabIds)
        assertEquals(announced, destination.attachedTabIds.size)
        assertEquals("orchard,harbor,another", destination.arguments[TaffyDestination.ATTACHED_TAB_IDS])
        assertNull(destination.shape)
        assertEquals(emptyList<TaffyDestination>(), navigator.replaced)
    }

    @Test
    fun `new workspace opens nothing when no eligible tab remains`() = runTest(dispatcher) {
        val browser = RecordingBrowser(initial = listOf(
            Tab(TabId("private"), "Private", "private.example", isPrivate = true),
        ))
        val navigator = RecordingNavigator()
        val viewModel = TabSwitcherViewModel(browser, IdleTasks(), NoAnalytics())
        assertFalse(viewModel.state.value.canStartWorkspace)

        viewModel.onIntent(TabSwitcherIntent.StartWorkspace, navigator)

        assertEquals(emptyList<TaffyDestination>(), navigator.pushed)
    }

    /**
     * Cards borrow the start page's local favicon store, so every open host
     * has to be asked of it without the person doing anything — or a tab
     * whose in-memory mark was never constructed draws with no face at all.
     */
    @Test
    fun `open tab hosts are asked of the favicon store unprompted`() = runTest(dispatcher) {
        val browser = RecordingBrowser(
            initial = listOf(
                Tab(TabId("tab_1"), "Retention policy", "docs.example.test", isSelected = true),
                Tab(TabId("tab_2"), "Product listing", "shop.example.test"),
            ),
        )
        TabSwitcherViewModel(browser, IdleTasks(), NoAnalytics())
        runCurrent()

        assertEquals(
            listOf(listOf("docs.example.test", "shop.example.test")),
            browser.markRequests,
        )
    }

    /**
     * Closing everything reads the tasks the core holds, not only who opened
     * a tab (decision 0236): the running task's tab stays, and a restored tab
     * with no creating task and one whose task is gone close with the
     * person's own.
     */
    @Test
    fun `closing everything keeps only a running task's tabs`() = runTest(dispatcher) {
        val browser = RecordingBrowser(
            initial = listOf(
                Tab(TabId("own"), "Retention policy", "docs.example.test", isSelected = true),
                Tab(TabId("live"), "Result", "live.example.test", isTaffyTab = true, taskId = "task-live"),
                Tab(TabId("restored"), "Result", "old.example.test", isTaffyTab = true, taskId = null),
                Tab(TabId("gone"), "Result", "gone.example.test", isTaffyTab = true, taskId = "task-gone"),
            ),
        )
        val tasks = RecordingTaskRepository().apply {
            publish(startedTask("task-live", "Compare prices", phase = TaskPhase.RUNNING))
        }
        val viewModel = TabSwitcherViewModel(browser, tasks, NoAnalytics())

        viewModel.onIntent(TabSwitcherIntent.ConfirmCloseVisible, RecordingNavigator())
        runCurrent()

        assertEquals(listOf(TabId("own"), TabId("restored"), TabId("gone")), browser.closed)
    }

    private class RecordingBrowser(
        initial: List<Tab> = emptyList(),
    ) : BrowserRepository {
        val opened = mutableListOf<Pair<String, Boolean>>()
        val selected = mutableListOf<TabId>()
        val closed = mutableListOf<TabId>()

        override val tabs: StateFlow<List<Tab>> = MutableStateFlow(initial)
        override val navigation: StateFlow<NavigationState> =
            MutableStateFlow(NavigationState(host = "", title = ""))
        override val pageAppearance: StateFlow<PageAppearance> = MutableStateFlow(PageAppearance())
        override val downloads: StateFlow<List<DownloadRecord>> = MutableStateFlow(emptyList())
        override val tabArtwork: StateFlow<Map<TabId, TabArtwork>> = MutableStateFlow(emptyMap())
        override val siteMarks: StateFlow<Map<String, Bitmap>> = MutableStateFlow(emptyMap())
        override val notice: StateFlow<BrowserNotice?> = MutableStateFlow(null)

        val markRequests = mutableListOf<List<String>>()

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

        override val filtering: StateFlow<FilteringSettings> =
            MutableStateFlow(FilteringSettings())

        override suspend fun setFilteringEnabled(enabled: Boolean) = Unit

        override suspend fun setSiteFilteringException(
        host: String,
        allow: Boolean,
        plane: SiteFilteringPlane,
    ): Boolean = true

        override suspend fun flushFilteringCounts() = Unit
    }

    private class RecordingNavigator : TaffyNavigator {
        val pushed = mutableListOf<TaffyDestination>()
        val replaced = mutableListOf<TaffyDestination>()

        override fun goTo(destination: TaffyDestination) {
            pushed += destination
        }

        override fun replaceCurrent(destination: TaffyDestination) {
            replaced += destination
        }

        override fun goBack(): Boolean = false

        override fun goHome() = Unit

        override fun restart(destination: TaffyDestination) = Unit

        override fun popWhile(shouldPop: (TaffyDestination) -> Boolean) = Unit
    }

    private class IdleTasks : TaskRepository {
        override val state: StateFlow<TaskRepositoryState> =
            MutableStateFlow(TaskRepositoryState(CoreUiAvailability.READY, 1u, null))

        override suspend fun startTask(
            goal: String,
            template: TaskTemplate,
            consent: TaskConsentIntent,
            workspaceId: String?,
        ) = TaffyResult.Success(Unit)

        override suspend fun cancelTask(taskId: String) = TaffyResult.Success(Unit)

        override suspend fun completeHandover(taskId: String) = TaffyResult.Success(Unit)

        override suspend fun supplyUserInput(taskId: String, answer: String) =
            TaffyResult.Success(Unit)

        override suspend fun approveAction(taskId: String, actionId: String) =
            TaffyResult.Success(Unit)

        override suspend fun retryCore() = TaffyResult.Success(Unit)
    }

    private class NoAnalytics : AnalyticsClient {
        override fun record(event: AnalyticsEvent) = Unit

        override fun recent(): List<AnalyticsEvent> = emptyList()
    }
}

// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.workspaces

import androidx.lifecycle.SavedStateHandle
import com.taffygo.browser.ui.core.analytics.AnalyticsClient
import com.taffygo.browser.ui.core.analytics.AnalyticsEvent
import com.taffygo.browser.ui.core.browser.BrowserProfilesRepository
import com.taffygo.browser.ui.core.common.TaffyResult
import com.taffygo.browser.ui.core.model.ExportFormat
import com.taffygo.browser.ui.core.model.FactId
import com.taffygo.browser.ui.core.model.SourceId
import com.taffygo.browser.ui.core.model.Workspace
import com.taffygo.browser.ui.core.model.WorkspaceExport
import com.taffygo.browser.ui.core.model.WorkspaceId
import com.taffygo.browser.ui.core.model.TaskTemplate
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyNavigator
import com.taffygo.browser.ui.core.workspace.WorkspaceRepository
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.launch
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.resetMain
import kotlinx.coroutines.test.runCurrent
import kotlinx.coroutines.test.runTest
import kotlinx.coroutines.test.setMain
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Before
import org.junit.Test

/**
 * Screen SCR-304's view model: the two ways out of the list, and the profile
 * line that says which profile the list belongs to (decision 0102).
 */
@OptIn(ExperimentalCoroutinesApi::class)
class WorkspaceListViewModelTest {
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
    fun `new workspace opens the sheet with the source table shape stated`() = runTest(dispatcher) {
        val navigator = RecordingNavigator()
        val viewModel = viewModel(FakeWorkspaces(), FakeProfiles())

        viewModel.onIntent(WorkspaceListIntent.Create, navigator)

        val sheet = navigator.visited.single() as TaffyDestination.AssistantBar
        assertEquals(TaskTemplate.BUILD_A_SOURCE_TABLE, sheet.shape)
    }

    @Test
    fun `opening a workspace goes to its detail`() = runTest(dispatcher) {
        val navigator = RecordingNavigator()
        val viewModel = viewModel(FakeWorkspaces(listOf(workspace("ws_0"))), FakeProfiles())

        viewModel.onIntent(WorkspaceListIntent.Open(WorkspaceId("ws_0")), navigator)

        assertEquals(
            listOf<TaffyDestination>(TaffyDestination.WorkspaceDetail("ws_0")),
            navigator.visited,
        )
    }

    @Test
    fun `the profile line appears once a second profile exists`() = runTest(dispatcher) {
        val profiles = FakeProfiles()
        val viewModel = viewModel(FakeWorkspaces(listOf(workspace("ws_0"))), profiles)
        backgroundScope.launch { viewModel.state.collect {} }
        runCurrent()

        assertNull(viewModel.state.value.profileName)

        profiles.snapshot.value = BrowserProfilesRepository.Snapshot(
            BrowserProfilesRepository.Availability.READY,
            listOf(
                BrowserProfilesRepository.Profile("p", "Personal", active = true),
                BrowserProfilesRepository.Profile("w", "Work", active = false),
            ),
        )
        runCurrent()

        assertEquals("Personal", viewModel.state.value.profileName)
        assertEquals(listOf("ws_0"), viewModel.state.value.workspaces.map { it.id.value })
    }

    @Test
    fun `showing the screen records it and reads the profiles`() = runTest(dispatcher) {
        val analytics = RecordingAnalytics()
        val profiles = FakeProfiles()
        val viewModel = viewModel(FakeWorkspaces(), profiles, analytics)

        viewModel.onShown()

        assertEquals(
            listOf<AnalyticsEvent>(
                AnalyticsEvent.ScreenShown(TaffyDestination.WorkspaceList.screenId),
            ),
            analytics.events,
        )
        assertEquals(1, profiles.refreshes)
    }

    private fun viewModel(
        workspaces: WorkspaceRepository,
        profiles: BrowserProfilesRepository,
        analytics: AnalyticsClient = RecordingAnalytics(),
    ) = WorkspaceListViewModel(workspaces, profiles, analytics, SavedStateHandle())

    private class FakeProfiles : BrowserProfilesRepository {
        override val snapshot = MutableStateFlow(
            BrowserProfilesRepository.Snapshot(
                BrowserProfilesRepository.Availability.READY,
                listOf(BrowserProfilesRepository.Profile("p", "Personal", active = true)),
            ),
        )
        var refreshes = 0

        override fun refresh() {
            refreshes++
        }

        override suspend fun create(displayName: String) = BrowserProfilesRepository.Result()
        override suspend fun activate(profileId: String) = BrowserProfilesRepository.Result()
        override suspend fun delete(profileId: String) = BrowserProfilesRepository.Result()
    }

    /** Only the list and its availability matter to SCR-304; the rest is unreachable here. */
    private class FakeWorkspaces(initial: List<Workspace> = emptyList()) : WorkspaceRepository {
        override val availability = MutableStateFlow(WorkspaceRepository.Availability.READY)
        override val workspaces = MutableStateFlow(initial)
        override val latestExport = MutableStateFlow<WorkspaceExport?>(null)

        override fun workspace(id: WorkspaceId): Workspace? =
            workspaces.value.firstOrNull { it.id == id }

        override suspend fun correctFact(
            id: WorkspaceId,
            factId: FactId,
            value: String,
        ): TaffyResult<Unit> = unreachable()

        override suspend fun excludeSource(id: WorkspaceId, sourceId: SourceId): TaffyResult<Unit> =
            unreachable()

        override fun renderExport(id: WorkspaceId, format: ExportFormat): String? = null

        override suspend fun requestExport(id: WorkspaceId, format: ExportFormat): TaffyResult<Unit> =
            unreachable()

        override suspend fun rename(
            id: WorkspaceId,
            expectedRevision: ULong,
            displayName: String,
        ): TaffyResult<Unit> = unreachable()

        override suspend fun delete(
            id: WorkspaceId,
            expectedRevision: ULong,
            confirmationToken: String,
        ): TaffyResult<Unit> = unreachable()

        private fun unreachable(): Nothing =
            throw UnsupportedOperationException("SCR-304 never asks for this")
    }

    private class RecordingAnalytics : AnalyticsClient {
        val events = mutableListOf<AnalyticsEvent>()

        override fun record(event: AnalyticsEvent) {
            events += event
        }

        override fun recent(): List<AnalyticsEvent> = events
    }

    private class RecordingNavigator : TaffyNavigator {
        val visited = mutableListOf<TaffyDestination>()

        override fun goTo(destination: TaffyDestination) {
            visited += destination
        }

        override fun replaceCurrent(destination: TaffyDestination) {
            visited += destination
        }

        override fun goBack() = true
        override fun goHome() = Unit
        override fun restart(destination: TaffyDestination) = Unit
        override fun popWhile(shouldPop: (TaffyDestination) -> Boolean) = Unit
    }
}

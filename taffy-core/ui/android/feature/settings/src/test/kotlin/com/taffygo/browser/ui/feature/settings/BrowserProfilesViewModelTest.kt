// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

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
import com.taffygo.browser.ui.core.workspace.WorkspaceRepository
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.collect
import kotlinx.coroutines.launch
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.resetMain
import kotlinx.coroutines.test.runCurrent
import kotlinx.coroutines.test.runTest
import kotlinx.coroutines.test.setMain
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test

/**
 * SCR-708's intents read facts only the projection carries. The reducer once
 * ran against the local slice, which starts LOADING and empty, so "Add
 * profile" and "Delete" did nothing on a phone (verification report section
 * 2.8). These cases pin the view model, not the reducer, because the reducer's
 * own tests hand it a ready state and could not see the defect.
 *
 * 1.0 ships one profile (decision 0255), so nothing here creates or switches
 * one; the last case holds the screen to that.
 */
@OptIn(ExperimentalCoroutinesApi::class)
class BrowserProfilesViewModelTest {
    private val dispatcher = StandardTestDispatcher()

    @Before
    fun setUp() = Dispatchers.setMain(dispatcher)

    @After
    fun tearDown() = Dispatchers.resetMain()

    @Test
    fun `asking to delete names the other profile as the candidate`() = runTest(dispatcher) {
        val repository = FakeProfiles(ready(ACTIVE, OTHER))
        val viewModel = viewModel(repository)
        backgroundScope.launch { viewModel.state.collect() }
        runCurrent()

        viewModel.onIntent(BrowserProfilesIntent.AskToDelete(OTHER.id))
        runCurrent()

        assertEquals(OTHER, viewModel.state.value.deleteCandidate)
    }

    @Test
    fun `the active profile is never a delete candidate`() = runTest(dispatcher) {
        val repository = FakeProfiles(ready(ACTIVE, OTHER))
        val viewModel = viewModel(repository)
        backgroundScope.launch { viewModel.state.collect() }
        runCurrent()

        viewModel.onIntent(BrowserProfilesIntent.AskToDelete(ACTIVE.id))
        runCurrent()

        assertNull(viewModel.state.value.deleteCandidate)
    }

    @Test
    fun `confirming a delete reaches the store with the candidate it named`() = runTest(dispatcher) {
        val repository = FakeProfiles(ready(ACTIVE, OTHER))
        val viewModel = viewModel(repository)
        backgroundScope.launch { viewModel.state.collect() }
        runCurrent()

        viewModel.onIntent(BrowserProfilesIntent.AskToDelete(OTHER.id))
        viewModel.onIntent(BrowserProfilesIntent.ConfirmDelete)
        runCurrent()

        assertEquals(listOf(OTHER.id), repository.deleted)
        assertNull(viewModel.state.value.deleteCandidate)
    }

    @Test
    fun `a profile that became active cannot be deleted by an old confirmation`() = runTest(dispatcher) {
        val repository = FakeProfiles(ready(ACTIVE, OTHER))
        val viewModel = viewModel(repository)
        viewModel.onIntent(BrowserProfilesIntent.AskToDelete(OTHER.id))
        repository.snapshot.value = ready(ACTIVE.copy(active = false), OTHER.copy(active = true))

        viewModel.onIntent(BrowserProfilesIntent.ConfirmDelete)
        runCurrent()

        assertTrue(repository.deleted.isEmpty())
    }

    @Test
    fun `a profile removed in another window cannot use an old confirmation`() = runTest(dispatcher) {
        val repository = FakeProfiles(ready(ACTIVE, OTHER))
        val viewModel = viewModel(repository)
        viewModel.onIntent(BrowserProfilesIntent.AskToDelete(OTHER.id))
        repository.snapshot.value = ready(ACTIVE)

        viewModel.onIntent(BrowserProfilesIntent.ConfirmDelete)
        runCurrent()

        assertTrue(repository.deleted.isEmpty())
    }

    @Test
    fun `no intent the screen can send creates or switches a profile`() = runTest(dispatcher) {
        val repository = FakeProfiles(ready(ACTIVE, OTHER))
        val viewModel = viewModel(repository)
        backgroundScope.launch { viewModel.state.collect() }
        runCurrent()

        listOf(
            BrowserProfilesIntent.Refresh,
            BrowserProfilesIntent.AskToDelete(OTHER.id),
            BrowserProfilesIntent.DismissDelete,
            BrowserProfilesIntent.DismissFailure,
        ).forEach(viewModel::onIntent)
        runCurrent()

        assertTrue(repository.created.isEmpty())
        assertTrue(repository.activated.isEmpty())
        assertTrue(repository.deleted.isEmpty())
    }

    private fun viewModel(repository: BrowserProfilesRepository) = BrowserProfilesViewModel(
        repository = repository,
        workspaces = NoWorkspaces(),
        analytics = NoAnalytics(),
    )

    private fun ready(vararg profiles: BrowserProfilesRepository.Profile) =
        BrowserProfilesRepository.Snapshot(
            availability = BrowserProfilesRepository.Availability.READY,
            profiles = profiles.toList(),
        )

    private class FakeProfiles(
        initial: BrowserProfilesRepository.Snapshot,
    ) : BrowserProfilesRepository {
        override val snapshot = MutableStateFlow(initial)
        val created = mutableListOf<String>()
        val activated = mutableListOf<String>()
        val deleted = mutableListOf<String>()

        override fun refresh() = Unit

        override suspend fun create(displayName: String): BrowserProfilesRepository.Result {
            created += displayName
            return BrowserProfilesRepository.Result()
        }

        override suspend fun activate(profileId: String): BrowserProfilesRepository.Result {
            activated += profileId
            return BrowserProfilesRepository.Result()
        }

        override suspend fun delete(profileId: String): BrowserProfilesRepository.Result {
            deleted += profileId
            snapshot.value = snapshot.value.copy(
                profiles = snapshot.value.profiles.filterNot { it.id == profileId },
            )
            return BrowserProfilesRepository.Result()
        }
    }

    private class NoWorkspaces : WorkspaceRepository {
        override val availability: StateFlow<WorkspaceRepository.Availability> =
            MutableStateFlow(WorkspaceRepository.Availability.LOADING)
        override val workspaces: StateFlow<List<Workspace>> = MutableStateFlow(emptyList())
        override val latestExport: StateFlow<WorkspaceExport?> = MutableStateFlow(null)

        override fun workspace(id: WorkspaceId): Workspace? = null

        override suspend fun correctFact(id: WorkspaceId, factId: FactId, value: String): TaffyResult<Unit> =
            throw UnsupportedOperationException("not part of SCR-708")

        override suspend fun excludeSource(id: WorkspaceId, sourceId: SourceId): TaffyResult<Unit> =
            throw UnsupportedOperationException("not part of SCR-708")

        override fun renderExport(id: WorkspaceId, format: ExportFormat): String? = null

        override suspend fun requestExport(id: WorkspaceId, format: ExportFormat): TaffyResult<Unit> =
            throw UnsupportedOperationException("not part of SCR-708")

        override suspend fun rename(
            id: WorkspaceId,
            expectedRevision: ULong,
            displayName: String,
        ): TaffyResult<Unit> = throw UnsupportedOperationException("not part of SCR-708")

        override suspend fun delete(
            id: WorkspaceId,
            expectedRevision: ULong,
            confirmationToken: String,
        ): TaffyResult<Unit> = throw UnsupportedOperationException("not part of SCR-708")
    }

    private class NoAnalytics : AnalyticsClient {
        override fun record(event: AnalyticsEvent) = Unit
        override fun recent(): List<AnalyticsEvent> = emptyList()
    }

    private companion object {
        val ACTIVE = BrowserProfilesRepository.Profile(id = "p-1", displayName = "Work", active = true)
        val OTHER = BrowserProfilesRepository.Profile(id = "p-2", displayName = "Home", active = false)
    }
}

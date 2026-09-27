// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.downloads

import com.taffygo.browser.ui.core.analytics.AnalyticsEvent
import com.taffygo.browser.ui.core.model.DownloadAction
import com.taffygo.browser.ui.core.model.DownloadId
import com.taffygo.browser.ui.core.model.DownloadRecord
import com.taffygo.browser.ui.core.model.DownloadState
import com.taffygo.browser.ui.core.ui.TaffyDestination
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.flow.collect
import kotlinx.coroutines.launch
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.resetMain
import kotlinx.coroutines.test.runCurrent
import kotlinx.coroutines.test.runTest
import kotlinx.coroutines.test.setMain
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test

@OptIn(ExperimentalCoroutinesApi::class)
class DownloadsViewModelTest {
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
    fun initialQueryAndBoundedSnapshotHaveDistinctTruthfulStates() = runTest(dispatcher) {
        val repository = FakeDownloadRepository()
        val viewModel = viewModel(repository)
        backgroundScope.launch { viewModel.state.collect {} }
        runCurrent()

        assertTrue(viewModel.state.value.isLoading)

        repository.snapshot.value = DownloadSnapshot.bounded(
            downloads = records(),
            status = DownloadCollectionStatus.COMPLETE,
        )
        runCurrent()

        assertFalse(viewModel.state.value.isLoading)
        assertEquals(3, viewModel.state.value.totalCount)
        assertEquals(3, viewModel.state.value.visibleCount)
    }

    @Test
    fun searchFilterSortAndGroupingReprojectWithoutCallingTheRepository() = runTest(dispatcher) {
        val repository = FakeDownloadRepository(
            DownloadSnapshot.bounded(records(), DownloadCollectionStatus.COMPLETE),
        )
        val viewModel = viewModel(repository)
        backgroundScope.launch { viewModel.state.collect {} }
        runCurrent()

        viewModel.onIntent(DownloadsIntent.SetQuery("docs"))
        viewModel.onIntent(DownloadsIntent.SelectFilter(DownloadFilter.ACTIVE))
        viewModel.onIntent(DownloadsIntent.SelectSort(DownloadSort.NAME))
        viewModel.onIntent(DownloadsIntent.SelectGrouping(DownloadGrouping.SOURCE))
        runCurrent()

        val state = viewModel.state.value
        assertEquals(1, state.visibleCount)
        assertEquals("report.pdf", state.groups.single().downloads.single().fileName)
        assertEquals(DownloadGrouping.SOURCE, state.grouping)
        assertTrue(repository.actions.isEmpty())
    }

    @Test
    fun everyManualActionCrossesTheSingleRepositorySeamWithTheExactId() = runTest(dispatcher) {
        val repository = FakeDownloadRepository(
            DownloadSnapshot.bounded(records(), DownloadCollectionStatus.COMPLETE),
        )
        val viewModel = viewModel(repository)

        viewModel.onIntent(DownloadsIntent.Pause(DownloadId("running")))
        viewModel.onIntent(DownloadsIntent.Resume(DownloadId("failed")))
        viewModel.onIntent(DownloadsIntent.Cancel(DownloadId("running")))
        viewModel.onIntent(DownloadsIntent.Open(DownloadId("complete")))
        viewModel.onIntent(DownloadsIntent.Share(DownloadId("complete")))
        viewModel.onIntent(DownloadsIntent.Remove(DownloadId("failed")))
        runCurrent()

        assertEquals(
            listOf(
                DownloadId("running") to DownloadAction.PAUSE,
                DownloadId("failed") to DownloadAction.RESUME,
                DownloadId("running") to DownloadAction.CANCEL,
                DownloadId("complete") to DownloadAction.OPEN,
                DownloadId("complete") to DownloadAction.SHARE,
                DownloadId("failed") to DownloadAction.REMOVE,
            ),
            repository.actions,
        )
    }

    @Test
    fun aStaleActionBecomesVisibleAndNeverRetriesAutomatically() = runTest(dispatcher) {
        val repository = FakeDownloadRepository().apply { acceptsActions = false }
        val viewModel = viewModel(repository)
        backgroundScope.launch { viewModel.state.collect {} }

        viewModel.onIntent(DownloadsIntent.Resume(DownloadId("gone")))
        runCurrent()

        assertEquals(DownloadActionNotice.NO_LONGER_AVAILABLE, viewModel.state.value.actionNotice)
        assertEquals(1, repository.actions.size)
        runCurrent()
        assertEquals(1, repository.actions.size)
    }

    @Test
    fun screenAnalyticsUsesTheCatalogIdentity() {
        val analytics = DownloadTestAnalytics()
        val viewModel = viewModel(analytics = analytics)

        viewModel.onShown()

        assertEquals(
            listOf(AnalyticsEvent.ScreenShown(TaffyDestination.Downloads.screenId)),
            analytics.events,
        )
    }

    private fun viewModel(
        repository: FakeDownloadRepository = FakeDownloadRepository(),
        analytics: DownloadTestAnalytics = DownloadTestAnalytics(),
    ) = DownloadsViewModel(
        downloads = repository,
        parts = FakeDownloadParts(),
        analytics = analytics,
        dispatchers = DownloadTestDispatchers(dispatcher),
    )

    private fun records() = listOf(
        record("complete", "photo.jpg", "media.example", DownloadState.COMPLETE),
        record("running", "report.pdf", "docs.example", DownloadState.RUNNING),
        record("failed", "notes.txt", "docs.example", DownloadState.FAILED),
    )

    private fun record(id: String, name: String, host: String, state: DownloadState) =
        DownloadRecord(
            id = DownloadId(id),
            fileName = name,
            host = host,
            totalBytes = 100,
            downloadedBytes = 50,
            state = state,
            allowedActions = emptySet(),
        )
}

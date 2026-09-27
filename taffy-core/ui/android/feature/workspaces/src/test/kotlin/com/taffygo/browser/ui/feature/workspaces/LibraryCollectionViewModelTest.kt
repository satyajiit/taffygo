// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.workspaces

import androidx.lifecycle.SavedStateHandle
import com.taffygo.browser.ui.core.analytics.AnalyticsClient
import com.taffygo.browser.ui.core.analytics.AnalyticsEvent
import com.taffygo.browser.ui.core.common.TaffyResult
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyNavigator
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.resetMain
import kotlinx.coroutines.test.runCurrent
import kotlinx.coroutines.test.runTest
import kotlinx.coroutines.test.setMain
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Before
import org.junit.Test

@OptIn(ExperimentalCoroutinesApi::class)
class LibraryCollectionViewModelTest {
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
    fun `only the exact approved refresh preview reaches the repository`() = runTest(dispatcher) {
        val preview = LibraryRepository.RefreshPreview(
            previewId = "ab".repeat(32),
            collectionId = "collection-1",
            libraryRevision = 7uL,
            workspaceRevision = 3uL,
            navigationCount = 1u,
            observationCount = 1u,
            sources = listOf(
                LibraryRepository.RefreshSource("source-1", "Evidence", "example.test"),
            ),
        )
        val repository = RecordingRefreshRepository(preview)
        val viewModel = LibraryCollectionViewModel(
            repository,
            NoOpAnalytics,
            SavedStateHandle(mapOf(TaffyDestination.COLLECTION_ID to preview.collectionId)),
        )

        viewModel.onIntent(LibraryCollectionIntent.ApproveRefresh(preview), NoOpNavigator)
        runCurrent()

        assertEquals(listOf(preview), repository.approved)
    }

    private class RecordingRefreshRepository(
        preview: LibraryRepository.RefreshPreview,
    ) : LibraryRepository {
        val approved = mutableListOf<LibraryRepository.RefreshPreview>()
        override val snapshot = MutableStateFlow<LibraryRepository.Snapshot>(
            LibraryRepository.Snapshot.Ready(
                collections = listOf(
                    libraryCollection(id = preview.collectionId).copy(refreshPreview = preview),
                ),
                revision = preview.libraryRevision,
            ),
        )
        override val latestExport = MutableStateFlow<LibraryRepository.Export?>(null)
        override val canKeep = true
        override val canMutate = true

        override suspend fun startRefresh(
            preview: LibraryRepository.RefreshPreview,
        ): TaffyResult<Unit> {
            approved += preview
            return TaffyResult.Success(Unit)
        }
    }

    private data object NoOpAnalytics : AnalyticsClient {
        override fun record(event: AnalyticsEvent) = Unit
        override fun recent(): List<AnalyticsEvent> = emptyList()
    }

    private data object NoOpNavigator : TaffyNavigator {
        override fun goTo(destination: TaffyDestination) = Unit
        override fun replaceCurrent(destination: TaffyDestination) = Unit
        override fun goBack() = true
        override fun goHome() = Unit
        override fun restart(destination: TaffyDestination) = Unit
        override fun popWhile(shouldPop: (TaffyDestination) -> Boolean) = Unit
    }
}

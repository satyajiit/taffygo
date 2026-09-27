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
import com.taffygo.browser.ui.core.model.ExportFormat
import com.taffygo.browser.ui.core.model.FactId
import com.taffygo.browser.ui.core.model.SourceId
import com.taffygo.browser.ui.core.model.WorkspaceExport
import com.taffygo.browser.ui.core.model.WorkspaceId
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyNavigator
import com.taffygo.browser.ui.core.workspace.WorkspaceRepository
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
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test

@OptIn(ExperimentalCoroutinesApi::class)
class ExportSheetViewModelTest {
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
    fun `successful write receives exact output but state and analytics do not`() =
        runTest(dispatcher) {
            val content = "# Workspace\n" + "x".repeat(8_000)
            val repository = ExportRepository(content)
            val analytics = RecordingAnalytics()
            val savedState = SavedStateHandle(
                mapOf(TaffyDestination.WORKSPACE_ID to repository.id.value),
            )
            val viewModel = ExportSheetViewModel(
                repository,
                EmptyLibraryRepository(),
                analytics,
                savedState,
            )
            val navigator = NoOpNavigator()
            runCurrent()

            viewModel.onIntent(ExportSheetIntent.Export, navigator)
            viewModel.onIntent(ExportSheetIntent.DestinationSelected, navigator)
            var handedToWriter: String? = null
            viewModel.writeExport(ExportFormat.MARKDOWN) {
                handedToWriter = it
                true
            }
            runCurrent()

            assertEquals(content, handedToWriter)
            assertTrue(viewModel.state.value.preview.length <= 4_096)
            assertEquals(ExportStatus.SUCCEEDED, viewModel.state.value.exportStatus)
            assertEquals(
                listOf(AnalyticsEvent.ArtifactExported(ExportFormat.MARKDOWN.label)),
                analytics.events,
            )
            assertEquals(setOf(TaffyDestination.WORKSPACE_ID), savedState.keys())
        }

    @Test
    fun `cancel and provider failure remain retryable and record no export`() =
        runTest(dispatcher) {
            val repository = ExportRepository("# Workspace\n")
            val analytics = RecordingAnalytics()
            val viewModel = ExportSheetViewModel(
                repository,
                EmptyLibraryRepository(),
                analytics,
                SavedStateHandle(mapOf(TaffyDestination.WORKSPACE_ID to repository.id.value)),
            )
            val navigator = NoOpNavigator()
            runCurrent()

            viewModel.onIntent(ExportSheetIntent.Export, navigator)
            viewModel.onIntent(ExportSheetIntent.DestinationCancelled, navigator)
            assertEquals(ExportStatus.CANCELLED, viewModel.state.value.exportStatus)
            assertTrue(viewModel.state.value.canExport)

            viewModel.onIntent(ExportSheetIntent.Export, navigator)
            viewModel.onIntent(ExportSheetIntent.DestinationSelected, navigator)
            viewModel.writeExport(ExportFormat.MARKDOWN) { false }
            runCurrent()

            assertEquals(ExportStatus.FAILED, viewModel.state.value.exportStatus)
            assertTrue(viewModel.state.value.canExport)
            assertTrue(analytics.events.isEmpty())
        }

    @Test
    fun `library collection export reaches the same exact trusted writer path`() =
        runTest(dispatcher) {
            val content = "# Library\n\n## Saved evidence"
            val library = LibraryExportRepository(content)
            val savedState = SavedStateHandle(
                mapOf(TaffyDestination.COLLECTION_ID to library.collectionId),
            )
            val viewModel = ExportSheetViewModel(
                ExportRepository("unused workspace output"),
                library,
                RecordingAnalytics(),
                savedState,
            )
            val navigator = NoOpNavigator()
            runCurrent()

            viewModel.onIntent(ExportSheetIntent.Export, navigator)
            viewModel.onIntent(ExportSheetIntent.DestinationSelected, navigator)
            var handedToWriter: String? = null
            viewModel.writeExport(ExportFormat.MARKDOWN) {
                handedToWriter = it
                true
            }
            runCurrent()

            assertEquals(content, handedToWriter)
            assertEquals(ExportStatus.SUCCEEDED, viewModel.state.value.exportStatus)
            assertEquals(setOf(TaffyDestination.COLLECTION_ID), savedState.keys())
        }

    private class ExportRepository(
        private val content: String,
    ) : WorkspaceRepository {
        val id = WorkspaceId("ws_export")
        override val availability = MutableStateFlow(WorkspaceRepository.Availability.READY)
        override val workspaces = MutableStateFlow(listOf(workspace(id = id.value)))
        override val latestExport = MutableStateFlow(
            WorkspaceExport(
                requestId = "export-test",
                workspaceId = id,
                revision = 1uL,
                format = ExportFormat.MARKDOWN,
                content = content,
            ),
        )

        override fun workspace(id: WorkspaceId) = workspaces.value.firstOrNull { it.id == id }

        override suspend fun correctFact(id: WorkspaceId, factId: FactId, value: String) =
            TaffyResult.Success(Unit)

        override suspend fun excludeSource(id: WorkspaceId, sourceId: SourceId) =
            TaffyResult.Success(Unit)

        override fun renderExport(id: WorkspaceId, format: ExportFormat): String? =
            content.takeIf { id == this.id && format == ExportFormat.MARKDOWN }

        override suspend fun requestExport(id: WorkspaceId, format: ExportFormat) =
            TaffyResult.Success(Unit)

        override suspend fun rename(
            id: WorkspaceId,
            expectedRevision: ULong,
            displayName: String,
        ) = TaffyResult.Success(Unit)

        override suspend fun delete(
            id: WorkspaceId,
            expectedRevision: ULong,
            confirmationToken: String,
        ) = TaffyResult.Success(Unit)
    }

    private class LibraryExportRepository(
        private val content: String,
    ) : LibraryRepository {
        val collectionId = "col_export"
        override val snapshot = MutableStateFlow<LibraryRepository.Snapshot>(
            LibraryRepository.Snapshot.Ready(
                collections = listOf(libraryCollection(id = collectionId)),
                revision = 1uL,
            ),
        )
        override val latestExport = MutableStateFlow(
            LibraryRepository.Export(
                collectionId = collectionId,
                revision = 1uL,
                format = ExportFormat.MARKDOWN,
                content = content,
            ),
        )
        override val canKeep: Boolean = true
        override val canMutate: Boolean = true

        override fun renderExport(collectionId: String?, format: ExportFormat): String? =
            content.takeIf {
                collectionId == this.collectionId && format == ExportFormat.MARKDOWN
            }

        override suspend fun requestExport(collectionId: String?, format: ExportFormat) =
            TaffyResult.Success(Unit)
    }

    private class RecordingAnalytics : AnalyticsClient {
        val events = mutableListOf<AnalyticsEvent>()

        override fun record(event: AnalyticsEvent) {
            events += event
        }

        override fun recent(): List<AnalyticsEvent> = events
    }

    private class NoOpNavigator : TaffyNavigator {
        override fun goTo(destination: TaffyDestination) = Unit
        override fun replaceCurrent(destination: TaffyDestination) = Unit
        override fun goBack() = true
        override fun goHome() = Unit
        override fun restart(destination: TaffyDestination) = Unit
        override fun popWhile(shouldPop: (TaffyDestination) -> Boolean) = Unit
    }
}

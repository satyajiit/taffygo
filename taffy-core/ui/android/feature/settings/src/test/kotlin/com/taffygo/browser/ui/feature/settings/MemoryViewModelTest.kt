// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import androidx.lifecycle.SavedStateHandle
import com.taffygo.browser.ui.core.analytics.AnalyticsClient
import com.taffygo.browser.ui.core.analytics.AnalyticsEvent
import com.taffygo.browser.ui.core.common.FailureReason
import com.taffygo.browser.ui.core.common.TaffyResult
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.launch
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.resetMain
import kotlinx.coroutines.test.runCurrent
import kotlinx.coroutines.test.runTest
import kotlinx.coroutines.test.setMain
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test

@OptIn(ExperimentalCoroutinesApi::class)
class MemoryViewModelTest {

    private val dispatcher = StandardTestDispatcher()

    @Before
    fun setUp() = Dispatchers.setMain(dispatcher)

    @After
    fun tearDown() = Dispatchers.resetMain()

    @Test
    fun `a rejected save keeps the exact draft and reports why`() = runTest(dispatcher) {
        val repository = FakeMemoryRepository(TaffyResult.Failure(FailureReason.STALE_REVISION))
        val viewModel = viewModel(repository)
        backgroundScope.launch { viewModel.state.collect {} }
        runCurrent()

        viewModel.onIntent(MemoryIntent.Open("memory-1"))
        viewModel.onIntent(MemoryIntent.ChangeText("Keep this exact draft"))
        viewModel.onIntent(MemoryIntent.Save)
        runCurrent()

        assertEquals("Keep this exact draft", viewModel.state.value.editor?.text)
        assertEquals(FailureReason.STALE_REVISION, viewModel.state.value.mutationFailure?.reason)
        assertEquals(
            MemoryUiState.MutationOperation.SAVE,
            viewModel.state.value.mutationFailure?.operation,
        )
        assertFalse(viewModel.state.value.mutationInFlight)
    }

    @Test
    fun `a rejected delete keeps the editor and confirmation visible`() = runTest(dispatcher) {
        val repository = FakeMemoryRepository(TaffyResult.Failure(FailureReason.CORE_UNAVAILABLE))
        val viewModel = viewModel(repository)
        backgroundScope.launch { viewModel.state.collect {} }
        runCurrent()

        viewModel.onIntent(MemoryIntent.Open("memory-1"))
        viewModel.onIntent(MemoryIntent.Delete)
        viewModel.onIntent(MemoryIntent.ConfirmDelete)
        runCurrent()

        assertNotNull(viewModel.state.value.editor)
        assertTrue(viewModel.state.value.confirmDelete)
        assertEquals(FailureReason.CORE_UNAVAILABLE, viewModel.state.value.mutationFailure?.reason)
        assertEquals(
            MemoryUiState.MutationOperation.DELETE,
            viewModel.state.value.mutationFailure?.operation,
        )
        assertFalse(viewModel.state.value.mutationInFlight)
    }

    @Test
    fun `an accepted save closes the editor only after the repository answers`() =
        runTest(dispatcher) {
            val repository = FakeMemoryRepository(TaffyResult.Success(Unit))
            val viewModel = viewModel(repository)
            backgroundScope.launch { viewModel.state.collect {} }
            runCurrent()

            viewModel.onIntent(MemoryIntent.Open("memory-1"))
            viewModel.onIntent(MemoryIntent.ChangeText("  Accepted draft  "))
            viewModel.onIntent(MemoryIntent.Save)
            runCurrent()

            assertNull(viewModel.state.value.editor)
            assertNull(viewModel.state.value.mutationFailure)
            assertEquals(
                SaveCall("memory-1", "Accepted draft", 7uL, 3uL),
                repository.saveCalls.single(),
            )
        }

    @Test
    fun `editor typing reuses the current visible note projection`() = runTest(dispatcher) {
        val notes = CountingList(
            List(40) { index ->
                memoryNote(id = "memory-$index", statement = "Note $index")
            },
        )
        val viewModel = viewModel(FakeMemoryRepository(TaffyResult.Success(Unit), notes))
        backgroundScope.launch { viewModel.state.collect {} }
        runCurrent()

        viewModel.onIntent(MemoryIntent.Open("memory-0"))
        runCurrent()
        val readsAfterOpen = notes.elementReads
        repeat(8) { index ->
            viewModel.onIntent(MemoryIntent.ChangeText("Draft $index"))
            runCurrent()
        }

        assertEquals(readsAfterOpen, notes.elementReads)
    }

    private fun viewModel(repository: MemoryRepository) = MemoryViewModel(
        repository = repository,
        analytics = NoAnalytics(),
        savedState = SavedStateHandle(),
    )

    private class FakeMemoryRepository(
        private val mutationResult: TaffyResult<Unit>,
        notes: List<MemoryRepository.Note> = listOf(memoryNote()),
    ) : MemoryRepository {
        val saveCalls = mutableListOf<SaveCall>()

        override val snapshot: StateFlow<MemoryRepository.Snapshot> = MutableStateFlow(
            MemoryRepository.Snapshot(
                availability = YouSurfaceAvailability.READY,
                revision = 7uL,
                notes = notes,
            ),
        )

        override suspend fun upsertYouWrote(
            id: String?,
            statement: String,
            expectedMemoryRevision: ULong,
            expectedRecordRevision: ULong,
        ): TaffyResult<Unit> {
            saveCalls += SaveCall(
                id,
                statement,
                expectedMemoryRevision,
                expectedRecordRevision,
            )
            return mutationResult
        }

        override suspend fun delete(
            id: String,
            expectedMemoryRevision: ULong,
            expectedRecordRevision: ULong,
        ): TaffyResult<Unit> = mutationResult
    }

    private class NoAnalytics : AnalyticsClient {
        override fun record(event: AnalyticsEvent) = Unit
        override fun recent(): List<AnalyticsEvent> = emptyList()
    }

    private class CountingList<T>(private val values: List<T>) : AbstractList<T>() {
        var elementReads: Int = 0
            private set

        override val size: Int
            get() = values.size

        override fun get(index: Int): T {
            elementReads += 1
            return values[index]
        }
    }

    private data class SaveCall(
        val id: String?,
        val statement: String,
        val expectedMemoryRevision: ULong,
        val expectedRecordRevision: ULong,
    )

    private companion object {
        fun memoryNote(
            id: String = "memory-1",
            statement: String = "Original",
        ) = MemoryRepository.Note(
            id = id,
            revision = 3uL,
            statement = statement,
            source = MemoryRepository.Source.YOU_WROTE,
            addedEpochDay = 20_000L,
        )
    }
}

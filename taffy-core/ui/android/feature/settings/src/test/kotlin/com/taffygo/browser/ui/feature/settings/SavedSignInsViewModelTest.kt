// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import androidx.lifecycle.SavedStateHandle
import com.taffygo.browser.ui.core.analytics.AnalyticsClient
import com.taffygo.browser.ui.core.analytics.AnalyticsEvent
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
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

@OptIn(ExperimentalCoroutinesApi::class)
class SavedSignInsViewModelTest {

    private val dispatcher = StandardTestDispatcher()

    @Before
    fun setUp() = Dispatchers.setMain(dispatcher)

    @After
    fun tearDown() = Dispatchers.resetMain()

    @Test
    fun `confirmed delete uses only the opaque id`() = runTest(dispatcher) {
        val repository = HoldingSignInsRepository()
        val viewModel = SavedSignInsViewModel(
            repository = repository,
            analytics = NoAnalytics(),
            savedState = SavedStateHandle(),
        )
        backgroundScope.launch { viewModel.state.collect {} }
        runCurrent()
        viewModel.onIntent(SavedSignInsIntent.Open("s1"))
        viewModel.onIntent(SavedSignInsIntent.Delete)
        viewModel.onIntent(SavedSignInsIntent.ConfirmDelete)
        runCurrent()
        assertEquals(listOf("s1"), repository.deleted)
        assertNull(viewModel.state.value.opened)
    }

    private class HoldingSignInsRepository : SavedSignInsRepository {
        val deleted = mutableListOf<String>()
        override val snapshot: StateFlow<SavedSignInsRepository.Snapshot> =
            MutableStateFlow(
                SavedSignInsRepository.Snapshot(
                    availability = YouSurfaceAvailability.READY,
                    revision = 7uL,
                    records = listOf(YouFixtures.signInRecord),
                ),
            ).asStateFlow()

        override suspend fun delete(id: String) {
            deleted += id
        }
    }

    private class NoAnalytics : AnalyticsClient {
        override fun record(event: AnalyticsEvent) = Unit
        override fun recent(): List<AnalyticsEvent> = emptyList()
    }
}

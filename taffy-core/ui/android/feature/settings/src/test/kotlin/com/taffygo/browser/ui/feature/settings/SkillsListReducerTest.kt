// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import androidx.lifecycle.SavedStateHandle
import com.taffygo.browser.ui.core.analytics.AnalyticsClient
import com.taffygo.browser.ui.core.analytics.AnalyticsEvent
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyNavigator
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.ExperimentalCoroutinesApi
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

/** Screen SCR-601's projection, search, and Empty-port toggle. */
@OptIn(ExperimentalCoroutinesApi::class)
class SkillsListReducerTest {

    private val dispatcher = StandardTestDispatcher()

    @Before
    fun setUp() = Dispatchers.setMain(dispatcher)

    @After
    fun tearDown() = Dispatchers.resetMain()

    @Test
    fun `built-in abilities look installed and search is offered`() {
        val state = projectSkillsList(
            SkillsRepository.Snapshot(
                availability = SkillsRepository.Availability.READY,
                skills = SkillsRepository.previewBuiltIns(),
            ),
            query = "",
        )
        assertEquals(SkillsRepository.Availability.READY, state.availability)
        assertTrue(state.showSearch)
        assertTrue(state.showNoExtras)
        assertEquals(SkillsRepository.Group.entries.toList(), state.groups.map { it.group })
        assertEquals(
            SkillsRepository.previewBuiltIns().size,
            state.groups.sumOf { it.skills.size },
        )
        assertTrue(state.groups.flatMap { it.skills }.all { it.enabled })
    }

    @Test
    fun `unavailable means no list and no extras caption`() {
        val state = projectSkillsList(
            SkillsRepository.Snapshot(
                availability = SkillsRepository.Availability.UNAVAILABLE,
                skills = SkillsRepository.previewBuiltIns(),
            ),
            query = "",
        )
        assertTrue(state.groups.isEmpty())
        assertFalse(state.showSearch)
        assertFalse(state.showNoExtras)
    }

    @Test
    fun `typing changes the query and matching is case-insensitive`() {
        val before = projectSkillsList(
            SkillsRepository.Snapshot(
                availability = SkillsRepository.Availability.READY,
                skills = SkillsRepository.previewBuiltIns(),
            ),
            query = "",
        )
        val after = reduceSkillsList(before, SkillsListIntent.QueryChanged("PDF"))
        assertEquals("PDF", after.query)
        val hits = after.matching { id ->
            when (id) {
                SkillsRepository.PDF_ANALYSIS ->
                    listOf("Read this PDF", "Answers questions about a PDF")
                else -> listOf("other")
            }
        }
        assertEquals(
            listOf(SkillsRepository.PDF_ANALYSIS),
            hits.flatMap { g -> g.skills.map { it.id } },
        )
    }

    @Test
    fun `toggling a built-in on the Empty port flips only that row`() = runTest(dispatcher) {
        val skills = EmptySkillsRepository()
        val viewModel = SkillsListViewModel(skills, NoAnalytics(), SavedStateHandle())
        val navigator = RecordingNavigator()
        backgroundScope.launch { viewModel.state.collect {} }
        runCurrent()

        viewModel.onIntent(SkillsListIntent.Toggle(SkillsRepository.FORM_ASSISTANT), navigator)
        runCurrent()

        val form = skills.snapshot.value.skills.first {
            it.id == SkillsRepository.FORM_ASSISTANT
        }
        assertFalse(form.enabled)
        assertTrue(form.builtIn)
        assertTrue(
            skills.snapshot.value.skills
                .filter { it.id != SkillsRepository.FORM_ASSISTANT }
                .all { it.enabled },
        )
        assertTrue(navigator.opened.isEmpty())
    }

    @Test
    fun `opening a row goes to its detail`() = runTest(dispatcher) {
        val viewModel = SkillsListViewModel(
            EmptySkillsRepository(),
            NoAnalytics(),
            SavedStateHandle(),
        )
        val navigator = RecordingNavigator()
        backgroundScope.launch { viewModel.state.collect {} }
        runCurrent()

        viewModel.onIntent(SkillsListIntent.Open(SkillsRepository.LIBRARY_BUILDER), navigator)
        assertEquals(
            listOf(TaffyDestination.SkillDetail(SkillsRepository.LIBRARY_BUILDER)),
            navigator.opened,
        )
    }

    @Test
    fun `unavailable toggle does not mint an ability`() = runTest(dispatcher) {
        val skills = UnavailableSkillsRepository()
        val viewModel = SkillsListViewModel(skills, NoAnalytics(), SavedStateHandle())
        backgroundScope.launch { viewModel.state.collect {} }
        runCurrent()

        viewModel.onIntent(
            SkillsListIntent.Toggle(SkillsRepository.FORM_ASSISTANT),
            RecordingNavigator(),
        )
        runCurrent()

        assertTrue(skills.snapshot.value.skills.isEmpty())
        assertEquals(SkillsRepository.Availability.UNAVAILABLE, skills.snapshot.value.availability)
    }

    private class RecordingNavigator : TaffyNavigator {
        val opened = mutableListOf<TaffyDestination>()
        override fun goTo(destination: TaffyDestination) {
            opened += destination
        }
        override fun replaceCurrent(destination: TaffyDestination) = Unit
        override fun goBack(): Boolean = false
        override fun goHome() = Unit
        override fun restart(destination: TaffyDestination) = Unit
        override fun popWhile(shouldPop: (TaffyDestination) -> Boolean) = Unit
    }

    private class NoAnalytics : AnalyticsClient {
        override fun record(event: AnalyticsEvent) = Unit
        override fun recent(): List<AnalyticsEvent> = emptyList()
    }
}

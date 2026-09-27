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
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test

/** Screen SCR-602's projection and honest remove. */
@OptIn(ExperimentalCoroutinesApi::class)
class SkillDetailReducerTest {

    private val dispatcher = StandardTestDispatcher()

    @Before
    fun setUp() = Dispatchers.setMain(dispatcher)

    @After
    fun tearDown() = Dispatchers.resetMain()

    @Test
    fun `a known built-in is projected as installed`() {
        val state = projectSkillDetail(
            SkillsRepository.Snapshot(
                availability = SkillsRepository.Availability.READY,
                skills = SkillsRepository.previewBuiltIns(),
            ),
            SkillsRepository.FORM_ASSISTANT,
        )
        val skill = requireNotNull(state.skill)
        assertEquals(SkillsRepository.FORM_ASSISTANT, skill.id)
        assertTrue(skill.enabled)
        assertTrue(skill.builtIn)
        assertEquals(listOf(SkillsRepository.MayUse.FORM), skill.mayUse)
    }

    @Test
    fun `an unknown id is missing rather than invented`() {
        val state = projectSkillDetail(
            SkillsRepository.Snapshot(
                availability = SkillsRepository.Availability.READY,
                skills = SkillsRepository.previewBuiltIns(),
            ),
            "not-a-skill",
        )
        assertNull(state.skill)
        assertEquals(SkillsRepository.Availability.READY, state.availability)
    }

    @Test
    fun `removing a built-in is refused and the row stays`() = runTest(dispatcher) {
        val skills = EmptySkillsRepository()
        val viewModel = SkillDetailViewModel(
            skills,
            NoAnalytics(),
            SavedStateHandle(mapOf(TaffyDestination.SKILL_ID to SkillsRepository.PDF_ANALYSIS)),
        )
        backgroundScope.launch { viewModel.state.collect {} }
        runCurrent()

        viewModel.onIntent(SkillDetailIntent.Remove)
        runCurrent()

        assertEquals(SkillsRepository.RemoveResult.CANNOT_REMOVE, viewModel.state.value.removeResult)
        assertNotNull(
            skills.snapshot.value.skills.firstOrNull { it.id == SkillsRepository.PDF_ANALYSIS },
        )
    }

    @Test
    fun `toggling on Empty flips the ability without granting a new one`() = runTest(dispatcher) {
        val skills = EmptySkillsRepository()
        val viewModel = SkillDetailViewModel(
            skills,
            NoAnalytics(),
            SavedStateHandle(
                mapOf(TaffyDestination.SKILL_ID to SkillsRepository.LIBRARY_BUILDER),
            ),
        )
        backgroundScope.launch { viewModel.state.collect {} }
        runCurrent()
        assertTrue(requireNotNull(viewModel.state.value.skill).enabled)

        viewModel.onIntent(SkillDetailIntent.Toggle)
        runCurrent()

        assertFalse(requireNotNull(viewModel.state.value.skill).enabled)
        assertEquals(SkillsRepository.previewBuiltIns().size, skills.snapshot.value.skills.size)
    }

    private class NoAnalytics : AnalyticsClient {
        override fun record(event: AnalyticsEvent) = Unit
        override fun recent(): List<AnalyticsEvent> = emptyList()
    }
}

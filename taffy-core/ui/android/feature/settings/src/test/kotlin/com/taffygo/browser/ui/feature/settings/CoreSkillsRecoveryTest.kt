// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.advanceUntilIdle
import kotlinx.coroutines.test.runTest
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import taffy.core_api.BuiltinSkillIdView
import taffy.core_api.CoreStatusProjectionMode
import taffy.core_api.SiteSkillStatusView

@OptIn(ExperimentalCoroutinesApi::class)
internal class CoreSkillsRecoveryTest {

    @Test
    fun `recovery keeps exact built ins and makes added abilities unavailable`() = runTest {
        val dispatcher = StandardTestDispatcher(testScheduler)
        val core = RecordingAssistantConfigurationCoreApiClient(
            assistantStatus(
                projectionMode = CoreStatusProjectionMode.RECOVERY_REQUIRED,
                siteSkills = listOf(assistantSiteSkill(status = SiteSkillStatusView.ACTIVE)),
            ),
        )
        val lifetime = assistantLifetime(dispatcher)
        val skills = CoreSkillsRepository(core, lifetime)
        advanceUntilIdle()

        val snapshot = skills.snapshot.value
        assertEquals(SkillsRepository.Availability.READY, snapshot.availability)
        assertEquals(BuiltinSkillIdView.entries.size, snapshot.skills.size)
        assertFalse(snapshot.siteSkillsAvailable)
        assertTrue(snapshot.skills.none { it.id == "repeat-checkout" })
        assertEquals(
            SkillsRepository.Readiness.REQUIRED_TOOL_UNAVAILABLE,
            snapshot.skillById(SkillsRepository.DOWNLOAD_ORGANIZER).readiness,
        )

        skills.setEnabled(SkillsRepository.DOWNLOAD_ORGANIZER, enabled = false)
        assertTrue(core.writes.isEmpty())
        assertEquals(
            SkillsRepository.RemoveResult.UNAVAILABLE,
            skills.remove("repeat-checkout"),
        )
        lifetime.close()
    }

    @Test
    fun `malformed built in publication is unavailable instead of partially rendered`() = runTest {
        val dispatcher = StandardTestDispatcher(testScheduler)
        val core = RecordingAssistantConfigurationCoreApiClient(
            assistantStatus().copy(builtin_skills = emptyList()),
        )
        val lifetime = assistantLifetime(dispatcher)
        val skills = CoreSkillsRepository(core, lifetime)
        advanceUntilIdle()

        assertEquals(
            SkillsRepository.Availability.UNAVAILABLE,
            skills.snapshot.value.availability,
        )
        assertTrue(skills.snapshot.value.skills.isEmpty())
        lifetime.close()
    }
}

private fun SkillsRepository.Snapshot.skillById(id: String): SkillsRepository.Skill =
    skills.single { it.id == id }

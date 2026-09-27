// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import com.taffygo.browser.ui.core.api.toSavedFlowReview
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.advanceUntilIdle
import kotlinx.coroutines.test.runTest
import kotlinx.coroutines.test.runCurrent
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertTrue
import org.junit.Test
import taffy.core_api.SiteSkillArgumentKind
import taffy.core_api.SiteSkillObservedArgument
import taffy.core_api.SiteSkillObservedStep

@OptIn(ExperimentalCoroutinesApi::class)
class CoreRecordedSkillReviewTest {
    @Test fun `recorded drafts cannot be enabled from an ordinary switch`() = runTest {
        val core = RecordingAssistantConfigurationCoreApiClient(assistantStatus(siteSkills = listOf(assistantSiteSkill())))
        val lifetime = assistantLifetime(StandardTestDispatcher(testScheduler))
        try {
            val skills = CoreSkillsRepository(core, lifetime)
            skills.setEnabled("repeat-checkout", true)
            assertTrue(core.skillMutations.isEmpty())
            val row = skills.snapshot.value.skills.single { it.id == "repeat-checkout" }
            assertTrue(row.needsRecordedReview)
            assertEquals(null, row.review)
        } finally { lifetime.close() }
    }

    @Test fun `review metadata publication reaches settings even with unchanged version and count`() = runTest {
        val before = assistantSiteSkill().copy(step_count = 1u)
        val after = reviewable()
        val core = RecordingAssistantConfigurationCoreApiClient(assistantStatus(siteSkills = listOf(before)))
        val lifetime = assistantLifetime(StandardTestDispatcher(testScheduler))
        try {
            val skills = CoreSkillsRepository(core, lifetime)
            core.publish(assistantStatus(siteSkills = listOf(after)))
            advanceUntilIdle()
            assertNotNull(skills.snapshot.value.skills.single { it.id == after.skill_id }.review)
            assertFalse(sameSkillsProjectionVersion(
                assistantStatus(siteSkills = listOf(before)), assistantStatus(siteSkills = listOf(after))))
        } finally { lifetime.close() }
    }

    @Test fun `acceptance names the reviewed version and a replacement is refused`() = runTest {
        val recorded = reviewable()
        val core = RecordingAssistantConfigurationCoreApiClient(assistantStatus(siteSkills = listOf(recorded)))
        val lifetime = assistantLifetime(StandardTestDispatcher(testScheduler))
        try {
            val skills = CoreSkillsRepository(core, lifetime)
            val review = requireNotNull(recorded.toSavedFlowReview())
            assertEquals(SkillsRepository.MutationResult.SUBMITTED, skills.acceptRecorded(review))
            assertEquals(recorded.active_version, core.skillMutations.single().expected_version)
            assertTrue(core.skillMutations.single().enabled)
            core.publish(assistantStatus(siteSkills = listOf(recorded.copy(active_version = 5u))))
            assertEquals(SkillsRepository.MutationResult.STALE, skills.acceptRecorded(review))
            assertEquals(1, core.skillMutations.size)
        } finally { lifetime.close() }
    }

    @Test fun `loading an omitted full review keeps the draft off until exact acceptance`() = runTest {
        val recorded = reviewable()
        val base = RecordingAssistantConfigurationCoreApiClient(assistantStatus(
            siteSkills = listOf(recorded.copy(reviewed_steps = emptyList())),
        ))
        val core = object : com.taffygo.browser.ui.core.api.CoreApiClient by base {
            override suspend fun getSavedFlowReview(requestId: String, skillId: String, expectedVersion: UInt) =
                taffy.core_api.SavedFlowQueryResult(requestId, status.value.generation,
                    taffy.core_api.SavedFlowQueryAvailability.AVAILABLE, listOf(recorded))
        }
        val lifetime = assistantLifetime(StandardTestDispatcher(testScheduler))
        try {
            val reviews = com.taffygo.browser.ui.core.api.SavedFlowReviewRepository(core, backgroundScope)
            val skills = CoreSkillsRepository(core, lifetime, reviews)
            val review = requireNotNull(recorded.toSavedFlowReview())
            assertEquals(SkillsRepository.MutationResult.STALE, skills.acceptRecorded(review))
            assertTrue(skills.loadRecordedReview(recorded.skill_id, recorded.active_version))
            runCurrent()
            assertNotNull(skills.snapshot.value.skills.single { it.id == recorded.skill_id }.review)
            assertTrue(base.skillMutations.isEmpty())
            assertEquals(SkillsRepository.MutationResult.SUBMITTED, skills.acceptRecorded(review))
            assertEquals(recorded.active_version, base.skillMutations.single().expected_version)
            base.publish(assistantStatus(siteSkills = listOf(recorded.copy(
                active_version = recorded.active_version + 1u, reviewed_steps = emptyList(),
            ))))
            assertEquals(SkillsRepository.MutationResult.STALE, skills.acceptRecorded(review))
        } finally { lifetime.close() }
    }

    private fun reviewable() = assistantSiteSkill().copy(
        step_count = 1u,
        recorded_from_task_id = "finished-task",
        reviewed_steps = listOf(SiteSkillObservedStep(
            verb = "browser.navigate", arguments = listOf(SiteSkillObservedArgument(
                parameter = 0u, kind = SiteSkillArgumentKind.PUBLIC_ADDRESS, value = 0uL, purpose = 0u,
                public_address = "https://shop.example/start", semantic_target = null,
            )), postcondition = 0u, has_fill = false, fill_purpose = 0u,
        )),
    )
}

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
import org.junit.Assert.assertNotEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import taffy.core_api.AssistantAbilityView
import taffy.core_api.CoreAvailability
import taffy.core_api.PersonalityPresetView
import taffy.core_api.SiteSkillMutationKind
import taffy.core_api.SiteSkillStatusView

@OptIn(ExperimentalCoroutinesApi::class)
internal class CoreAssistantConfigurationRepositoriesTest {

    @Test
    fun `published configuration projects exactly and survives adapter recreation`() = runTest {
        val dispatcher = StandardTestDispatcher(testScheduler)
        val core = RecordingAssistantConfigurationCoreApiClient(
            assistantStatus(
                revision = 18uL,
                disabled = listOf(AssistantAbilityView.FORM, AssistantAbilityView.VIDEO),
                preset = PersonalityPresetView.TRIP_PLANNER,
                scales = PersonalityRepository.Scales(1, 2, 1),
            ),
        )
        val firstLifetime = assistantLifetime(dispatcher)
        val firstSkills = CoreSkillsRepository(core, firstLifetime)
        val firstPersonality = CorePersonalityRepository(core, firstLifetime)
        advanceUntilIdle()

        assertFalse(firstSkills.snapshot.value.skill(SkillsRepository.FORM_ASSISTANT).enabled)
        assertFalse(
            firstSkills.snapshot.value
                .skill(SkillsRepository.VIDEO_TRANSCRIPT_ANALYZER)
                .enabled,
        )
        assertTrue(firstSkills.snapshot.value.skill(SkillsRepository.PDF_ANALYSIS).enabled)
        assertEquals(
            PersonalityRepository.Preset.TRIP_PLANNER,
            firstPersonality.snapshot.value.selected,
        )
        assertEquals(
            PersonalityRepository.Scales(1, 2, 1),
            firstPersonality.snapshot.value.scales,
        )
        firstLifetime.close()

        // A new window adapter starts from the same profile publication. It
        // does not fall back to process-local defaults after recreation.
        val secondLifetime = assistantLifetime(dispatcher)
        val restartedSkills = CoreSkillsRepository(core, secondLifetime)
        val restartedPersonality = CorePersonalityRepository(core, secondLifetime)
        assertFalse(
            restartedSkills.snapshot.value.skill(SkillsRepository.FORM_ASSISTANT).enabled,
        )
        assertEquals(
            PersonalityRepository.Preset.TRIP_PLANNER,
            restartedPersonality.snapshot.value.selected,
        )

        core.publish(assistantStatus(availability = CoreAvailability.STARTING))
        advanceUntilIdle()
        assertEquals(SkillsRepository.Availability.LOADING, restartedSkills.snapshot.value.availability)
        assertEquals(
            PersonalityRepository.Availability.LOADING,
            restartedPersonality.snapshot.value.availability,
        )

        core.publish(assistantStatus(availability = CoreAvailability.CIRCUIT_OPEN))
        advanceUntilIdle()
        assertEquals(
            SkillsRepository.Availability.UNAVAILABLE,
            restartedSkills.snapshot.value.availability,
        )
        assertEquals(
            PersonalityRepository.Availability.UNAVAILABLE,
            restartedPersonality.snapshot.value.availability,
        )
        secondLifetime.close()
    }

    @Test
    fun `invalid skill ids and unavailable state dispatch nothing`() = runTest {
        val dispatcher = StandardTestDispatcher(testScheduler)
        val core = RecordingAssistantConfigurationCoreApiClient(assistantStatus())
        val lifetime = assistantLifetime(dispatcher)
        val skills = CoreSkillsRepository(core, lifetime)

        skills.setEnabled("not-an-ability", enabled = false)
        assertEquals(SkillsRepository.RemoveResult.NOT_FOUND, skills.remove("not-an-ability"))
        assertEquals(
            SkillsRepository.RemoveResult.CANNOT_REMOVE,
            skills.remove(SkillsRepository.FORM_ASSISTANT),
        )
        assertTrue(core.writes.isEmpty())

        core.publish(assistantStatus(availability = CoreAvailability.UNAVAILABLE))
        advanceUntilIdle()
        skills.setEnabled(SkillsRepository.FORM_ASSISTANT, enabled = false)
        assertEquals(
            SkillsRepository.RemoveResult.UNAVAILABLE,
            skills.remove(SkillsRepository.FORM_ASSISTANT),
        )
        assertTrue(core.writes.isEmpty())
        lifetime.close()
    }

    @Test
    fun `whole record CAS waits for publication and leaves races to the core`() = runTest {
        val dispatcher = StandardTestDispatcher(testScheduler)
        val core = RecordingAssistantConfigurationCoreApiClient(assistantStatus(revision = 7uL))
        val lifetime = assistantLifetime(dispatcher)
        val skills = CoreSkillsRepository(core, lifetime)
        val personality = CorePersonalityRepository(core, lifetime)

        skills.setEnabled(SkillsRepository.FORM_ASSISTANT, enabled = false)
        personality.choosePreset(PersonalityRepository.Preset.QUICK_SHOPPER)

        assertEquals(2, core.writes.size)
        assertEquals(7uL, core.writes[0].expectedRevision)
        assertEquals(listOf(AssistantAbilityView.FORM), core.writes[0].disabledAbilities)
        assertEquals(7uL, core.writes[1].expectedRevision)
        assertTrue(core.writes[1].disabledAbilities.isEmpty())
        assertEquals(PersonalityPresetView.QUICK_SHOPPER, core.writes[1].preset)
        // Submission is not publication: neither adapter invents the result.
        assertTrue(skills.snapshot.value.skill(SkillsRepository.FORM_ASSISTANT).enabled)
        assertEquals(
            PersonalityRepository.Preset.CAREFUL_RESEARCHER,
            personality.snapshot.value.selected,
        )

        core.publish(
            assistantStatus(revision = 8uL, disabled = listOf(AssistantAbilityView.FORM)),
        )
        advanceUntilIdle()
        assertFalse(skills.snapshot.value.skill(SkillsRepository.FORM_ASSISTANT).enabled)
        assertEquals(
            PersonalityRepository.Preset.CAREFUL_RESEARCHER,
            personality.snapshot.value.selected,
        )

        personality.choosePreset(PersonalityRepository.Preset.QUICK_SHOPPER)
        val afterPublication = core.writes.last()
        assertEquals(8uL, afterPublication.expectedRevision)
        assertEquals(listOf(AssistantAbilityView.FORM), afterPublication.disabledAbilities)
        assertEquals(2u, afterPublication.pace)
        assertEquals(0u, afterPublication.length)
        assertEquals(2u, afterPublication.checkIn)
        lifetime.close()
    }

    @Test
    fun `scale writes are bounded preserve abilities and no-op exact state`() = runTest {
        val dispatcher = StandardTestDispatcher(testScheduler)
        val core = RecordingAssistantConfigurationCoreApiClient(
            assistantStatus(
                revision = 12uL,
                disabled = listOf(AssistantAbilityView.PDF),
                preset = PersonalityPresetView.TRIP_PLANNER,
                scales = PersonalityRepository.Scales(1, 1, 1),
            ),
        )
        val lifetime = assistantLifetime(dispatcher)
        val personality = CorePersonalityRepository(core, lifetime)

        personality.choosePreset(PersonalityRepository.Preset.TRIP_PLANNER)
        assertTrue(core.writes.isEmpty())

        personality.setScales(PersonalityRepository.Scales(-9, 99, 1))
        val write = core.writes.single()
        assertEquals(12uL, write.expectedRevision)
        assertEquals(listOf(AssistantAbilityView.PDF), write.disabledAbilities)
        assertEquals(PersonalityPresetView.TRIP_PLANNER, write.preset)
        assertEquals(0u, write.pace)
        assertEquals(2u, write.length)
        assertEquals(1u, write.checkIn)
        lifetime.close()
    }

    @Test
    fun `every compiled in skill has one contract ability`() {
        assertEquals(
            AssistantAbilityView.entries.toSet(),
            SkillsRepository.previewBuiltIns().mapNotNull { it.id.toAssistantAbility() }.toSet(),
        )
        assertEquals(AssistantAbilityView.entries.size, SkillsRepository.previewBuiltIns().size)
    }

    @Test
    fun `published site skill projects and exact mutations use its durable version`() = runTest {
        val dispatcher = StandardTestDispatcher(testScheduler)
        val core = RecordingAssistantConfigurationCoreApiClient(
            assistantStatus(
                siteSkills = listOf(assistantSiteSkill(status = SiteSkillStatusView.ACTIVE)),
            ),
        )
        val lifetime = assistantLifetime(dispatcher)
        val skills = CoreSkillsRepository(core, lifetime)
        advanceUntilIdle()

        val projected = skills.snapshot.value.skill("repeat-checkout")
        assertFalse(projected.builtIn)
        assertTrue(projected.enabled)
        assertEquals("https://shop.example", projected.origin)
        assertEquals(4u, projected.version)
        assertEquals(2u, projected.stepCount)

        skills.setEnabled(projected.id, enabled = false)
        assertEquals(SiteSkillMutationKind.SET_ENABLED, core.skillMutations.single().kind)
        assertEquals(4u, core.skillMutations.single().expected_version)
        assertFalse(core.skillMutations.single().enabled)

        assertEquals(SkillsRepository.RemoveResult.REMOVED, skills.remove(projected.id))
        assertEquals(SiteSkillMutationKind.REMOVE, core.skillMutations.last().kind)
        assertEquals(4u, core.skillMutations.last().expected_version)
        lifetime.close()
    }

    @Test
    fun `teach seam slugifies a plain name and carries only observed structures`() = runTest {
        val dispatcher = StandardTestDispatcher(testScheduler)
        val core = RecordingAssistantConfigurationCoreApiClient(assistantStatus())
        val lifetime = assistantLifetime(dispatcher)
        val skills = CoreSkillsRepository(core, lifetime)
        val recording = assistantObservedRecording()

        assertEquals(
            SkillsRepository.MutationResult.SUBMITTED,
            skills.teachSite("Repeat weekly checkout", recording),
        )
        val teach = core.skillMutations.single()
        assertEquals(SiteSkillMutationKind.TEACH, teach.kind)
        assertEquals("repeat-weekly-checkout", teach.skill_id)
        assertEquals(0u, teach.expected_version)
        assertEquals(recording.origin, teach.origin)
        assertEquals(recording.clauses, teach.clauses)
        assertEquals(recording.steps, teach.steps)
        assertEquals(teach.steps.size.toUInt(), teach.admitted)
        assertFalse(teach.enabled)

        assertEquals(
            SkillsRepository.MutationResult.INVALID,
            skills.teachSite("   ", recording),
        )
        assertEquals(1, core.skillMutations.size)
        lifetime.close()
    }

    @Test
    fun `update refuses a stale view before crossing the core seam`() = runTest {
        val dispatcher = StandardTestDispatcher(testScheduler)
        val core = RecordingAssistantConfigurationCoreApiClient(
            assistantStatus(siteSkills = listOf(assistantSiteSkill())),
        )
        val lifetime = assistantLifetime(dispatcher)
        val skills = CoreSkillsRepository(core, lifetime)

        assertEquals(
            SkillsRepository.MutationResult.STALE,
            skills.updateSite("repeat-checkout", 3u, assistantObservedRecording()),
        )
        assertTrue(core.skillMutations.isEmpty())
        assertEquals(
            SkillsRepository.MutationResult.SUBMITTED,
            skills.updateSite("repeat-checkout", 4u, assistantObservedRecording()),
        )
        assertEquals(SiteSkillMutationKind.UPDATE, core.skillMutations.single().kind)
        lifetime.close()
    }

    @Test
    fun `Skills and Personality ignore each other's unrelated configuration changes`() {
        val baseline = assistantStatus(siteSkills = listOf(assistantSiteSkill()))
        val personalityOnly = assistantStatus(
            revision = 2uL,
            preset = PersonalityPresetView.TRIP_PLANNER,
            siteSkills = baseline.site_skills.map { it.copy(updated_at_epoch_ms = 9uL) },
        )
        assertTrue(sameSkillsProjectionVersion(baseline, personalityOnly))
        assertNotEquals(
            baseline.personalityProjectionVersion(),
            personalityOnly.personalityProjectionVersion(),
        )

        val skillsOnly = baseline.copy(
            builtin_skills = baseline.builtin_skills.map { skill ->
                if (skill.required_ability == AssistantAbilityView.FORM) {
                    skill.copy(enabled = false)
                } else {
                    skill
                }
            },
        )
        assertFalse(sameSkillsProjectionVersion(baseline, skillsOnly))
        assertEquals(
            baseline.personalityProjectionVersion(),
            skillsOnly.personalityProjectionVersion(),
        )
    }

}

private fun SkillsRepository.Snapshot.skill(id: String): SkillsRepository.Skill =
    skills.single { it.id == id }

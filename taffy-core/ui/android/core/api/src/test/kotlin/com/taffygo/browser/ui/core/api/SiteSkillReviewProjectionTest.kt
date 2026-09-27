// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.api

import com.taffygo.browser.ui.core.model.SavedFlowReview
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import taffy.core_api.SiteSkillArgumentKind
import taffy.core_api.SiteSkillObservedArgument
import taffy.core_api.SiteSkillObservedStep
import taffy.core_api.SiteSkillProvenanceView
import taffy.core_api.SiteSkillSemanticTarget
import taffy.core_api.SiteSkillStatusView
import taffy.core_api.SiteSkillView

class SiteSkillReviewProjectionTest {
    @Test
    fun `review preserves full starting address ordered target and personal handover`() {
        val skill = recorded()
        val review = requireNotNull(skill.toSavedFlowReview())
        assertEquals("https://identity.example.test/download-document", review.startingAddress)
        assertEquals(listOf(SavedFlowReview.Action.OPEN_PAGE, SavedFlowReview.Action.HANDOVER,
            SavedFlowReview.Action.CHOOSE_CONTROL), review.steps.map { it.action })
        assertEquals(SavedFlowReview.PersonPurpose.IDENTITY_NUMBER, review.steps[1].personPurpose)
        assertEquals(SavedFlowReview.Target.DOWNLOAD, review.steps[2].target)
        val acceptance = review.acceptanceMutation()
        assertEquals(skill.active_version, acceptance.expected_version)
        assertEquals(skill.skill_id, acceptance.skill_id)
        assertTrue(acceptance.steps.isEmpty())
        assertTrue(acceptance.enabled)
    }

    @Test
    fun `legacy partial and unknown steps cannot silently become a complete review`() {
        val skill = recorded()
        assertNull(skill.copy(reviewed_steps = emptyList()).toSavedFlowReview())
        assertNull(skill.copy(step_count = 8u).toSavedFlowReview())
        assertNull(skill.copy(reviewed_steps = skill.reviewed_steps.mapIndexed { index, step ->
            if (index == 1) step.copy(verb = "unknown.future.action") else step
        }).toSavedFlowReview())
        assertTrue(skill.copy(reviewed_steps = emptyList()).needsRecordedReview())
    }

    @Test
    fun `addresses containing credentials queries or fragments do not appear in a save review`() {
        for (address in listOf("https://person:secret@identity.example.test/download",
            "https://identity.example.test/download?session=private",
            "https://identity.example.test/download#private", "http://identity.example.test/download")) {
            assertNull(recorded(address).toSavedFlowReview())
        }
    }

    @Test
    fun `live semantic link focus and download steps remain visible in order`() {
        val skill = recorded()
        for ((verb, action) in listOf(
            "browser.dom.focus" to SavedFlowReview.Action.FOCUS_CONTROL,
            "browser.link.open" to SavedFlowReview.Action.OPEN_LINK,
            "browser.download.from_link" to SavedFlowReview.Action.DOWNLOAD,
        )) {
            val changed = skill.copy(reviewed_steps = skill.reviewed_steps.dropLast(1) +
                skill.reviewed_steps.last().copy(verb = verb))
            assertEquals(action, requireNotNull(changed.toSavedFlowReview()).steps.last().action)
            assertEquals(SavedFlowReview.Target.DOWNLOAD,
                requireNotNull(changed.toSavedFlowReview()).steps.last().target)
        }
    }

    @Test
    fun `unrecognized click target withdraws review rather than hiding its meaning`() {
        val skill = recorded()
        val last = skill.reviewed_steps.last()
        val unknown = last.arguments.single().copy(semantic_target = SiteSkillSemanticTarget(0u, 999u))
        assertNull(skill.copy(reviewed_steps = skill.reviewed_steps.dropLast(1) +
            last.copy(arguments = listOf(unknown))).toSavedFlowReview())
    }

    private fun recorded(address: String = "https://identity.example.test/download-document") = SiteSkillView(
        skill_id = "download-document", origin = "https://identity.example.test",
        provenance = SiteSkillProvenanceView.RECORDED_FROM_TASK, status = SiteSkillStatusView.DRAFT,
        active_version = 3u, step_count = 3u, installed_at_epoch_ms = 1uL, updated_at_epoch_ms = 2uL,
        recorded_from_task_id = "completed-task",
        reviewed_steps = listOf(
            step("browser.navigate", argument(SiteSkillArgumentKind.PUBLIC_ADDRESS, address = address)),
            step("user.handover", argument(SiteSkillArgumentKind.FROM_PERSON, purpose = 14u)),
            step("browser.dom.click", argument(SiteSkillArgumentKind.SEMANTIC_TARGET,
                target = SiteSkillSemanticTarget(0u, 10u))),
        ),
    )

    private fun argument(kind: SiteSkillArgumentKind, address: String? = null,
        purpose: UInt = 0u, target: SiteSkillSemanticTarget? = null) = SiteSkillObservedArgument(
        parameter = 0u, kind = kind, value = 0uL, purpose = purpose,
        public_address = address, semantic_target = target,
    )

    private fun step(verb: String, argument: SiteSkillObservedArgument) = SiteSkillObservedStep(
        verb = verb, arguments = listOf(argument), postcondition = 0u, has_fill = false, fill_purpose = 0u,
    )
}

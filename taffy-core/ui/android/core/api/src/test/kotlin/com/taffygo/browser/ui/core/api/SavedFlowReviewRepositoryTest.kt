// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.api

import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.async
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.test.runCurrent
import kotlinx.coroutines.test.runTest
import org.junit.Assert.*
import org.junit.Test
import taffy.core_api.*

@OptIn(ExperimentalCoroutinesApi::class)
class SavedFlowReviewRepositoryTest {
    private val base = RecordingCoreApiClient()
    private val full = flow()
    private val publication = MutableStateFlow(base.status.value.copy(
        availability = CoreAvailability.READY, generation = 7uL,
        projection_mode = CoreStatusProjectionMode.COMPLETE,
        site_skills = listOf(full.copy(reviewed_steps = emptyList())),
    ))
    private var answer: suspend (String) -> SavedFlowQueryResult = { reply(it) }
    private val opened = mutableListOf<String>()
    private var opening: suspend () -> Unit = {}
    private val core = object : CoreApiClient by base {
        override val status = publication
        override suspend fun findSavedFlows(requestId: String, goal: String) = answer(requestId)
        override suspend fun getSavedFlowReview(requestId: String, skillId: String, expectedVersion: UInt) = answer(requestId)
        override suspend fun openSavedFlowStart(requestId: String, skillId: String, expectedVersion: UInt) {
            opened += skillId
            opening()
        }
    }

    @Test fun `exact full review fills omitted detail without changing metadata and disappears in next generation`() = runTest {
        val repository = SavedFlowReviewRepository(core, backgroundScope)
        assertTrue(repository.load(full.skill_id, full.active_version))
        assertEquals(full, repository.current().site_skills.single())
        assertTrue(publication.value.site_skills.single().reviewed_steps.isEmpty())
        val review = requireNotNull(full.toSavedFlowReview())
        assertTrue(repository.open(review))
        publication.value = publication.value.copy(generation = 8uL)
        assertTrue(repository.current().site_skills.single().reviewed_steps.isEmpty())
        assertFalse(repository.open(review))
        assertEquals(listOf(full.skill_id), opened)
    }

    @Test fun `version replacement during awaited read cannot restore the old body`() = runTest {
        val waiting = CompletableDeferred<Unit>()
        answer = { request -> waiting.await(); reply(request) }
        val repository = SavedFlowReviewRepository(core, backgroundScope)
        val loaded = async { repository.load(full.skill_id, full.active_version) }
        runCurrent()
        publication.value = publication.value.copy(site_skills = listOf(full.copy(
            active_version = 2u, reviewed_steps = emptyList(),
        )))
        waiting.complete(Unit)
        assertFalse(loaded.await())
        assertTrue(repository.current().site_skills.single().reviewed_steps.isEmpty())
    }

    @Test fun `wrong request generation private and partial replies never become reviews or an authoritative miss`() = runTest {
        val repository = SavedFlowReviewRepository(core, backgroundScope)
        val corruptions: List<(SavedFlowQueryResult) -> SavedFlowQueryResult> = listOf(
            { it.copy(request_id = "other") }, { it.copy(service_generation = 8uL) },
            { it.copy(availability = SavedFlowQueryAvailability.PRIVATE_PROFILE, flows = emptyList()) },
            { it.copy(flows = listOf(full.copy(step_count = 2u))) },
            { it.copy(flows = listOf(full, full)) },
        )
        for (corrupt in corruptions) {
            answer = { corrupt(reply(it)) }
            assertNull(repository.find("download document"))
            assertTrue(repository.current().site_skills.single().reviewed_steps.isEmpty())
        }
        answer = { reply(it).copy(flows = emptyList()) }
        assertEquals(emptyList<Any>(), repository.find("unmatched request"))
    }

    @Test fun `cache keeps at most four complete bodies`() = runTest {
        val flows = (1..5).map { flow("flow-$it") }
        publication.value = publication.value.copy(site_skills = flows.map { it.copy(reviewed_steps = emptyList()) })
        val repository = SavedFlowReviewRepository(core, backgroundScope)
        for (flow in flows) {
            answer = { reply(it).copy(flows = listOf(flow)) }
            assertTrue(repository.load(flow.skill_id, flow.active_version))
        }
        assertEquals(4, repository.current().site_skills.count { it.reviewed_steps.isNotEmpty() })
        assertTrue(repository.current().site_skills.first().reviewed_steps.isEmpty())
    }

    @Test fun `a later full review read cannot overtake an explicit navigation`() = runTest {
        val repository = SavedFlowReviewRepository(core, backgroundScope)
        assertTrue(repository.load(full.skill_id, full.active_version))
        val commit = CompletableDeferred<Unit>()
        opening = { commit.await() }
        val navigation = async { repository.open(requireNotNull(full.toSavedFlowReview())) }
        runCurrent()
        var reads = 0
        answer = { request -> reads++; reply(request) }
        val read = async { repository.load(full.skill_id, full.active_version) }
        runCurrent()
        assertEquals(0, reads)
        commit.complete(Unit)
        assertTrue(navigation.await())
        assertTrue(read.await())
        assertEquals(1, reads)
    }

    private fun reply(request: String) = SavedFlowQueryResult(request, 7uL, SavedFlowQueryAvailability.AVAILABLE, listOf(full))

    private fun flow(id: String = "download-document") = SiteSkillView(
        id, "https://identity.example.test", SiteSkillProvenanceView.RECORDED_FROM_TASK,
        SiteSkillStatusView.ACTIVE, 1u, 1u, 1uL, 1uL, "completed-task",
        listOf(SiteSkillObservedStep("browser.navigate", listOf(SiteSkillObservedArgument(
            0u, SiteSkillArgumentKind.PUBLIC_ADDRESS, 0uL, 0u,
            "https://identity.example.test/download", null,
        )), 0u, false, 0u)),
    )
}

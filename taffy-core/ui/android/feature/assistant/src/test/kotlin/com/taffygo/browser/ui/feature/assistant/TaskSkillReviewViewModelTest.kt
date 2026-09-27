// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.assistant

import com.taffygo.browser.ui.core.api.toSavedFlowReview
import com.taffygo.browser.ui.core.api.CoreApiClient
import com.taffygo.browser.ui.core.api.CoreApiSubmissionException
import com.taffygo.browser.ui.core.model.TaskTemplate
import com.taffygo.browser.ui.core.task.TaskProjection
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.launch
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.UnconfinedTestDispatcher
import kotlinx.coroutines.test.resetMain
import kotlinx.coroutines.test.runCurrent
import kotlinx.coroutines.test.runTest
import kotlinx.coroutines.test.setMain
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import taffy.core_api.CoreStatusProjectionMode
import taffy.core_api.SiteSkillArgumentKind
import taffy.core_api.SiteSkillMutationBody
import taffy.core_api.SiteSkillObservedArgument
import taffy.core_api.SiteSkillObservedStep
import taffy.core_api.SiteSkillProvenanceView
import taffy.core_api.SiteSkillStatusView
import taffy.core_api.SiteSkillView
import taffy.core_api.TaskPhase

@OptIn(ExperimentalCoroutinesApi::class)
class TaskSkillReviewViewModelTest {
    private val dispatcher = StandardTestDispatcher()
    private val tasks = TestTasks()
    private val base = RecordingComposerCoreApiClient()
    private val publication = MutableStateFlow(base.status.value.copy(site_skills = listOf(recorded())))
    private val writes = mutableListOf<SiteSkillMutationBody>()
    private var refused = false
    private val core = object : CoreApiClient by base {
        override val status = publication
        override suspend fun mutateSiteSkill(body: SiteSkillMutationBody) {
            if (refused) throw CoreApiSubmissionException(CoreApiSubmissionException.Reason.CORE_UNAVAILABLE)
            writes += body
        }
    }

    @Before fun setUp() {
        Dispatchers.setMain(dispatcher)
        tasks.status.value = tasks.status.value.copy(task = completed())
    }
    @After fun tearDown() = Dispatchers.resetMain()

    @Test fun `only exact successful task can offer a recording`() = runTest(dispatcher) {
        val model = TaskSkillReviewViewModel(core, tasks)
        backgroundScope.launch(UnconfinedTestDispatcher(testScheduler)) { model.state.collect {} }
        runCurrent()
        assertEquals("finished", model.state.value.taskId)
        tasks.status.value = tasks.status.value.copy(task = completed().copy(id = "unrelated"))
        runCurrent()
        assertNull(model.state.value.review)
        tasks.status.value = tasks.status.value.copy(task = completed().copy(phase = TaskPhase.PARTIAL))
        runCurrent()
        assertNull(model.state.value.review)
        tasks.status.value = tasks.status.value.copy(task = completed())
        publication.value = publication.value.copy(projection_mode = CoreStatusProjectionMode.RECOVERY_REQUIRED)
        runCurrent()
        assertNull(model.state.value.review)
    }

    @Test fun `save requires review and waits for exact active publication`() = runTest(dispatcher) {
        val model = TaskSkillReviewViewModel(core, tasks)
        backgroundScope.launch(UnconfinedTestDispatcher(testScheduler)) { model.state.collect {} }
        runCurrent()
        val flow = requireNotNull(model.state.value.review)
        model.accept(flow)
        runCurrent()
        assertTrue(writes.isEmpty())
        model.review(flow)
        model.accept(flow)
        model.accept(flow)
        runCurrent()
        assertEquals(1, writes.size)
        assertEquals(flow.version, writes.single().expected_version)
        assertFalse(model.state.value.saved)
        assertTrue(model.state.value.submitting)
        publication.value = publication.value.copy(site_skills = listOf(recorded().copy(status = SiteSkillStatusView.ACTIVE)))
        runCurrent()
        assertTrue(model.state.value.saved)
        assertFalse(model.state.value.showReview)
        assertFalse(model.state.value.submitting)
    }

    @Test fun `stale reviewed version cannot enable its replacement`() = runTest(dispatcher) {
        val model = TaskSkillReviewViewModel(core, tasks)
        backgroundScope.launch(UnconfinedTestDispatcher(testScheduler)) { model.state.collect {} }
        runCurrent()
        val old = requireNotNull(model.state.value.review)
        model.review(old)
        publication.value = publication.value.copy(site_skills = listOf(recorded().copy(active_version = 2u)))
        model.accept(old)
        runCurrent()
        assertTrue(writes.isEmpty())
        assertFalse(model.state.value.showReview)
    }

    @Test fun `not now does not activate and a refused submission remains retryable`() = runTest(dispatcher) {
        val model = TaskSkillReviewViewModel(core, tasks)
        backgroundScope.launch(UnconfinedTestDispatcher(testScheduler)) { model.state.collect {} }
        runCurrent()
        val flow = requireNotNull(model.state.value.review)
        model.dismiss(flow)
        runCurrent()
        assertNull(model.state.value.review)
        assertTrue(writes.isEmpty())
        model.review(flow)
        refused = true
        model.accept(flow)
        runCurrent()
        assertTrue(model.state.value.failed)
        assertFalse(model.state.value.submitting)
        refused = false
        model.accept(flow)
        runCurrent()
        assertEquals(1, writes.size)
    }

    @Test fun `omitted task review can be loaded but never saved before its full dialog opens`() = runTest(dispatcher) {
        publication.value = publication.value.copy(site_skills = listOf(recorded().copy(reviewed_steps = emptyList())))
        val reading = object : CoreApiClient by core {
            override suspend fun getSavedFlowReview(requestId: String, skillId: String, expectedVersion: UInt) =
                taffy.core_api.SavedFlowQueryResult(requestId, status.value.generation,
                    taffy.core_api.SavedFlowQueryAvailability.AVAILABLE, listOf(recorded()))
        }
        val reviews = com.taffygo.browser.ui.core.api.SavedFlowReviewRepository(reading, backgroundScope)
        val model = TaskSkillReviewViewModel(reading, tasks, reviews)
        backgroundScope.launch(UnconfinedTestDispatcher(testScheduler)) { model.state.collect {} }
        runCurrent()
        assertEquals(recorded().skill_id, model.state.value.skillId)
        assertNull(model.state.value.review)
        val complete = requireNotNull(recorded().toSavedFlowReview())
        model.accept(complete)
        assertTrue(writes.isEmpty())
        model.reviewCurrent()
        runCurrent()
        assertEquals(complete, model.state.value.review)
        assertTrue(model.state.value.showReview)
        assertTrue(writes.isEmpty())
        model.accept(complete)
        runCurrent()
        assertEquals(1, writes.size)
    }

    @Test fun `not now dismisses an omitted review without a fetch or save`() = runTest(dispatcher) {
        publication.value = publication.value.copy(site_skills = listOf(recorded().copy(reviewed_steps = emptyList())))
        val model = TaskSkillReviewViewModel(core, tasks)
        backgroundScope.launch(UnconfinedTestDispatcher(testScheduler)) { model.state.collect {} }
        runCurrent()
        assertEquals(recorded().skill_id, model.state.value.skillId)
        model.dismissCurrent()
        runCurrent()
        assertNull(model.state.value.skillId)
        assertTrue(writes.isEmpty())
    }

    private fun completed() = TaskProjection(
        id = "finished", revision = 4uL, phase = TaskPhase.COMPLETED, goal = "Download a document",
        template = TaskTemplate.WEB_ERRAND, progressBasisPoints = 10_000u,
        statusMessageKey = null, failure = null, pendingAction = null,
    )

    private fun recorded() = SiteSkillView(
        skill_id = "download-document", origin = "https://identity.example.test",
        provenance = SiteSkillProvenanceView.RECORDED_FROM_TASK, status = SiteSkillStatusView.DRAFT,
        active_version = 1u, step_count = 1u, installed_at_epoch_ms = 1uL, updated_at_epoch_ms = 1uL,
        recorded_from_task_id = "finished", reviewed_steps = listOf(SiteSkillObservedStep(
            verb = "browser.navigate", arguments = listOf(SiteSkillObservedArgument(
                parameter = 0u, kind = SiteSkillArgumentKind.PUBLIC_ADDRESS, value = 0uL, purpose = 0u,
                public_address = "https://identity.example.test/download", semantic_target = null,
            )), postcondition = 0u, has_fill = false, fill_purpose = 0u,
        )),
    )
}

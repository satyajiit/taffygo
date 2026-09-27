// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.workspace.internal

import com.taffygo.browser.ui.core.common.AppDispatchers
import com.taffygo.browser.ui.core.common.CoroutineFailureSink
import com.taffygo.browser.ui.core.common.FailureReason
import com.taffygo.browser.ui.core.common.di.TaffyProfileLifetime
import com.taffygo.browser.ui.core.model.ExportFormat
import com.taffygo.browser.ui.core.model.FactId
import com.taffygo.browser.ui.core.model.FactKind
import com.taffygo.browser.ui.core.model.SourceId
import com.taffygo.browser.ui.core.model.TaskDisplayState
import com.taffygo.browser.ui.core.model.TaskTemplate
import com.taffygo.browser.ui.core.model.WorkspaceId
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.advanceUntilIdle
import kotlinx.coroutines.test.runTest
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import taffy.core_api.CoreAvailability
import taffy.core_api.CoreStatus
import taffy.core_api.CoreStatusProjectionMode
import taffy.core_api.TaskTemplateId
import taffy.core_api.WorkspaceExportFormat
import taffy.core_api.WorkspaceExportView
import taffy.core_api.WorkspaceFactKind
import taffy.core_api.WorkspaceFactView
import taffy.core_api.WorkspacePhase
import taffy.core_api.WorkspaceSourceView
import taffy.core_api.WorkspaceViewState

@OptIn(ExperimentalCoroutinesApi::class)
internal class CoreWorkspaceRepositoryTest {
    @Test
    fun `recovery projection hides rows and refuses mutations as core unavailable`() = runTest {
        val dispatcher = StandardTestDispatcher(testScheduler)
        val core = RecordingWorkspaceCoreApiClient(
            status(listOf(workspace())).copy(
                projection_mode = CoreStatusProjectionMode.RECOVERY_REQUIRED,
            ),
        )
        val lifetime = lifetime(dispatcher)
        val repository = CoreWorkspaceRepository(core, lifetime)
        advanceUntilIdle()

        assertEquals(
            com.taffygo.browser.ui.core.workspace.WorkspaceRepository.Availability.UNAVAILABLE,
            repository.availability.value,
        )
        assertTrue(repository.workspaces.value.isEmpty())
        assertEquals(
            FailureReason.CORE_UNAVAILABLE,
            repository.correctFact(
                WorkspaceId("workspace-1"),
                FactId("fact-1"),
                "hidden",
            ).reasonOrNull(),
        )
        assertNull(core.correction)
        lifetime.close()
    }

    @Test
    fun `status projects exact workspace truth and refuses unrepresentable time`() = runTest {
        val dispatcher = StandardTestDispatcher(testScheduler)
        val core = RecordingWorkspaceCoreApiClient(
            status(workspaces = listOf(workspace(), workspace(updatedAt = ULong.MAX_VALUE))),
        )
        val lifetime = lifetime(dispatcher)
        val repository = CoreWorkspaceRepository(core, lifetime)
        advanceUntilIdle()

        assertEquals(1, repository.workspaces.value.size)
        val projected = repository.workspaces.value.single()
        assertEquals(WorkspaceId("workspace-1"), projected.id)
        assertEquals(TaskDisplayState.PARTLY_DONE, projected.state)
        assertEquals(TaskTemplate.COMPARE_PRODUCTS, projected.template)
        assertEquals("example.test", projected.sources.single().host)
        assertEquals(FactKind.FROM_THE_PAGE, projected.facts.single().kind)
        assertTrue(projected.facts.single().needsANewSource)
        lifetime.close()
    }

    @Test
    fun `mutations carry the exact current revision and missing ids dispatch nothing`() = runTest {
        val dispatcher = StandardTestDispatcher(testScheduler)
        val core = RecordingWorkspaceCoreApiClient(status(listOf(workspace())))
        val lifetime = lifetime(dispatcher)
        val repository = CoreWorkspaceRepository(core, lifetime)

        repository.correctFact(WorkspaceId("workspace-1"), FactId("fact-1"), "corrected")
        repository.excludeSource(WorkspaceId("workspace-1"), SourceId("source-1"))
        assertEquals(7uL, core.correction?.revision)
        assertEquals("corrected", core.correction?.value)
        assertEquals(7uL, core.exclusion?.revision)

        core.correction = null
        repository.correctFact(WorkspaceId("missing"), FactId("fact-1"), "fabricated")
        assertNull(core.correction)
        lifetime.close()
    }

    @Test
    fun `export content only appears for an exact Rust completion`() = runTest {
        val dispatcher = StandardTestDispatcher(testScheduler)
        val initial = status(listOf(workspace()))
        val core = RecordingWorkspaceCoreApiClient(initial)
        val lifetime = lifetime(dispatcher)
        val repository = CoreWorkspaceRepository(core, lifetime)
        repository.requestExport(WorkspaceId("workspace-1"), ExportFormat.MARKDOWN)

        assertEquals(7uL, core.export?.revision)
        assertEquals(WorkspaceExportFormat.MARKDOWN, core.export?.format)
        assertTrue(core.export?.requestId?.startsWith("export-") == true)

        core.publish(
            initial.copy(
                workspace_export = WorkspaceExportView(
                    request_id = core.export?.requestId.orEmpty(),
                    workspace_id = "workspace-1",
                    revision = 7uL,
                    format = WorkspaceExportFormat.MARKDOWN,
                    content = "# Exact Rust export\n",
                ),
            ),
        )
        advanceUntilIdle()
        assertEquals(
            "# Exact Rust export\n",
            repository.renderExport(WorkspaceId("workspace-1"), ExportFormat.MARKDOWN),
        )
        assertNull(repository.renderExport(WorkspaceId("workspace-1"), ExportFormat.COMMA_SEPARATED))
        lifetime.close()
    }

    @Test
    fun `rename and delete submit only the exact published saved revision`() = runTest {
        val dispatcher = StandardTestDispatcher(testScheduler)
        val core = RecordingWorkspaceCoreApiClient(status(listOf(workspace())))
        val lifetime = lifetime(dispatcher)
        val repository = CoreWorkspaceRepository(core, lifetime)

        repository.rename(WorkspaceId("workspace-1"), 7uL, "Evidence review")
        assertEquals(7uL, core.rename?.revision)
        assertEquals("Evidence review", core.rename?.displayName)

        repository.delete(WorkspaceId("workspace-1"), 7uL, "a".repeat(64))
        assertEquals(7uL, core.deletion?.revision)
        assertEquals("a".repeat(64), core.deletion?.confirmationToken)

        core.rename = null
        val stale = repository.rename(WorkspaceId("workspace-1"), 6uL, "Old view")
        assertEquals(FailureReason.STALE_REVISION, stale.reasonOrNull())
        assertNull(core.rename)

        core.deletion = null
        val wrongConfirmation = repository.delete(
            WorkspaceId("workspace-1"),
            7uL,
            "b".repeat(64),
        )
        assertEquals(FailureReason.INVALID_REQUEST, wrongConfirmation.reasonOrNull())
        assertNull(core.deletion)
        lifetime.close()
    }

    @Test
    fun `profile repositories never share status`() = runTest {
        val dispatcher = StandardTestDispatcher(testScheduler)
        val firstLifetime = lifetime(dispatcher)
        val privateLifetime = lifetime(dispatcher)
        val regular = CoreWorkspaceRepository(
            RecordingWorkspaceCoreApiClient(status(listOf(workspace()))),
            firstLifetime,
        )
        val private = CoreWorkspaceRepository(
            RecordingWorkspaceCoreApiClient(status(emptyList())),
            privateLifetime,
        )
        advanceUntilIdle()

        assertEquals(listOf(WorkspaceId("workspace-1")), regular.workspaces.value.map { it.id })
        assertTrue(private.workspaces.value.isEmpty())
        firstLifetime.close()
        privateLifetime.close()
    }

    @Test
    fun `task drafts never appear as saved workspaces or accept workspace mutations`() = runTest {
        val dispatcher = StandardTestDispatcher(testScheduler)
        val core = RecordingWorkspaceCoreApiClient(
            status(listOf(workspace().copy(saved = false))),
        )
        val lifetime = lifetime(dispatcher)
        val repository = CoreWorkspaceRepository(core, lifetime)
        advanceUntilIdle()

        assertTrue(repository.workspaces.value.isEmpty())
        repository.correctFact(WorkspaceId("workspace-1"), FactId("fact-1"), "hidden")
        repository.excludeSource(WorkspaceId("workspace-1"), SourceId("source-1"))
        repository.rename(WorkspaceId("workspace-1"), 7uL, "Hidden")
        repository.delete(WorkspaceId("workspace-1"), 7uL, "a".repeat(64))
        assertNull(core.correction)
        assertNull(core.exclusion)
        assertNull(core.rename)
        assertNull(core.deletion)
        lifetime.close()
    }
}

private fun status(workspaces: List<WorkspaceViewState>) = CoreStatus(
    availability = CoreAvailability.READY,
    generation = 3uL,
    active_tasks = emptyList(),
    auth_state = null,
    workspaces = workspaces,
    workspace_export = null,
    asset_delivery = null,
    provider_roster = emptyList(),
    provider_probes = emptyList(),
    provider_models = emptyList(),
    library = taffy.core_api.LibraryViewState(
        availability = taffy.core_api.LibraryAvailability.AVAILABLE,
        revision = 0uL,
        entries = emptyList(),
        search = null,
        refresh_previews = emptyList(),
        refresh_results = emptyList(),
    ),
    library_export = null,
    memory = taffy.core_api.MemoryViewState(
        availability = taffy.core_api.MemoryAvailability.AVAILABLE,
        revision = 0uL,
        records = emptyList(),
        search = null,
    ),
    assistant_configuration = taffy.core_api.AssistantConfigurationView(
        revision = 0uL,
        disabled_abilities = emptyList(),
        preset = taffy.core_api.PersonalityPresetView.CAREFUL_RESEARCHER,
        pace = 0u,
        length = 1u,
        check_in = 0u,
    ),
    saved_sign_ins = taffy.core_api.SavedSignInsView(
        availability = taffy.core_api.SavedDataAvailability.UNAVAILABLE,
        revision = 0uL,
        records = emptyList(),
    ),
    saved_details = taffy.core_api.SavedDetailsView(
        availability = taffy.core_api.SavedDataAvailability.UNAVAILABLE,
        revision = 0uL,
        people = emptyList(),
    ),
    site_skills = emptyList(),
    builtin_skills = emptyList(),
    projection_mode = taffy.core_api.CoreStatusProjectionMode.COMPLETE,
    projection_omissions = emptyList(),
)

private fun workspace(updatedAt: ULong = 1_767_225_600_000uL) = WorkspaceViewState(
    workspace_id = "workspace-1",
    revision = 7uL,
    goal = "compare evidence",
    phase = WorkspacePhase.PARTLY_DONE,
    last_updated_epoch_ms = updatedAt,
    template_id = TaskTemplateId.COMPARE_PRODUCTS,
    sources = listOf(
        WorkspaceSourceView("source-1", "Evidence", "example.test", 1_767_225_600_000uL, 1u, true),
    ),
    facts = listOf(
        WorkspaceFactView(
            "fact-1",
            "price",
            "10",
            WorkspaceFactKind.FROM_PAGE,
            listOf("source-1"),
            null,
            false,
            true,
        ),
    ),
    saved = true,
    display_name = "Evidence comparison",
    deletion_preview = taffy.core_api.WorkspaceDeletionPreviewView(
        sources = 1u,
        facts = 1u,
        artifact_metadata = 0u,
        derived_indexes = 0u,
        confirmation_token = "a".repeat(64),
    ),
)

private fun lifetime(dispatcher: CoroutineDispatcher) = TaffyProfileLifetime(
    dispatchers = object : AppDispatchers {
        override val main = dispatcher
        override val default = dispatcher
        override val io = dispatcher
    },
    failureSink = CoroutineFailureSink { },
)

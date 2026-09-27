// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import com.taffygo.browser.ui.core.common.FailureReason
import com.taffygo.browser.ui.core.common.TaffyResult
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.test.advanceUntilIdle
import kotlinx.coroutines.test.runCurrent
import kotlinx.coroutines.test.runTest
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import taffy.core_api.AssistantConfigurationView
import taffy.core_api.CoreAvailability
import taffy.core_api.CoreStatus
import taffy.core_api.MemoryAvailability
import taffy.core_api.MemoryRecordView
import taffy.core_api.MemorySearchHitView
import taffy.core_api.MemorySearchView
import taffy.core_api.MemoryScopeKind
import taffy.core_api.MemorySensitivity
import taffy.core_api.MemorySourceKind
import taffy.core_api.MemoryViewState
import taffy.core_api.MemoryWorkspaceView
import taffy.core_api.PersonalityPresetView

@OptIn(ExperimentalCoroutinesApi::class)
internal class CoreMemoryRepositoryTest {

    @Test
    fun `published records retain attribution scope sensitivity and expiry`() = runTest {
        val core = RecordingAssistantConfigurationCoreApiClient(
            memoryStatus(records = listOf(memoryRecord())),
        )
        val repository = CoreMemoryRepository(core, backgroundScope)
        advanceUntilIdle()

        val snapshot = repository.snapshot.value
        assertEquals(27uL, snapshot.revision)
        assertEquals(YouSurfaceAvailability.READY, snapshot.availability)
        val note = snapshot.notes.single()
        assertEquals("memory-1", note.id)
        assertEquals(4uL, note.revision)
        assertEquals(MemoryRepository.Source.TAFFY_NOTICED, note.source)
        assertEquals("task-9", note.sourceTaskId)
        assertEquals("Research", note.workspaceName)
        assertEquals(MemoryRepository.Scope.WORKSPACE, note.scope)
        assertEquals("Research", note.scopeWorkspaceName)
        assertTrue(note.sensitive)
        assertEquals(20_500L, note.expiresEpochDay)
    }

    @Test
    fun `editing and deleting use the exact published revisions`() = runTest {
        val core = RecordingAssistantConfigurationCoreApiClient(
            memoryStatus(records = listOf(memoryRecord())),
        )
        val repository = CoreMemoryRepository(core, backgroundScope)

        repository.upsertYouWrote("memory-1", "  Prefer concise answers.  ", 27uL, 4uL)
        repository.delete("memory-1", 27uL, 4uL)

        val upsert = core.memoryUpserts.single()
        assertEquals("memory-1", upsert.memoryId)
        assertEquals("Prefer concise answers.", upsert.statement)
        assertEquals(MemoryScopeKind.WORKSPACE, upsert.scopeKind)
        assertEquals("workspace-7", upsert.scopeWorkspace?.workspace_id)
        assertEquals(MemorySensitivity.SENSITIVE, upsert.sensitivity)
        assertEquals(27uL, upsert.expectedMemoryRevision)
        assertEquals(4uL, upsert.expectedRecordRevision)
        assertEquals(1_771_200_000_000uL, upsert.expiresAtEpochMillis)

        val deletion = core.memoryDeletions.single()
        assertEquals("memory-1", deletion.memoryId)
        assertEquals(27uL, deletion.expectedMemoryRevision)
        assertEquals(4uL, deletion.expectedRecordRevision)
    }

    @Test
    fun `search delegates a bounded request and projects only the current revision`() = runTest {
        val core = RecordingAssistantConfigurationCoreApiClient(
            memoryStatus(
                records = listOf(memoryRecord()),
                search = MemorySearchView(
                    request_id = "search-1",
                    query = "concise",
                    memory_revision = 27uL,
                    hits = listOf(MemorySearchHitView("memory-1")),
                ),
            ),
        )
        val repository = CoreMemoryRepository(core, backgroundScope)
        advanceUntilIdle()

        assertEquals(setOf("memory-1"), repository.snapshot.value.search?.memoryIds)
        repository.search("concise")
        val search = core.memorySearches.single()
        assertEquals("concise", search.query)
        assertEquals(32u, search.limit)
        assertTrue(search.requestId.matches(Regex("memory-search-[0-9a-f]{64}")))

        core.publish(
            memoryStatus(
                records = listOf(memoryRecord()),
                search = MemorySearchView(
                    request_id = "stale-search",
                    query = "concise",
                    memory_revision = 26uL,
                    hits = listOf(MemorySearchHitView("memory-1")),
                ),
            ),
        )
        runCurrent()
        assertEquals(null, repository.snapshot.value.search)
    }

    @Test
    fun `private and unavailable Memory never publish or mutate rows`() = runTest {
        val privateCore = RecordingAssistantConfigurationCoreApiClient(
            memoryStatus(memoryAvailability = MemoryAvailability.PRIVATE_PROFILE),
        )
        val privateRepository = CoreMemoryRepository(privateCore, backgroundScope)
        advanceUntilIdle()

        assertEquals(YouSurfaceAvailability.UNAVAILABLE, privateRepository.snapshot.value.availability)
        assertTrue(privateRepository.snapshot.value.notes.isEmpty())
        privateRepository.upsertYouWrote(null, "Never persist this", 0uL, 0uL)
        privateRepository.delete("memory-1", 0uL, 1uL)
        privateRepository.search("never")
        assertTrue(privateCore.memoryUpserts.isEmpty())
        assertTrue(privateCore.memoryDeletions.isEmpty())
        assertTrue(privateCore.memorySearches.isEmpty())

        val unavailableCore = RecordingAssistantConfigurationCoreApiClient(
            memoryStatus(coreAvailability = CoreAvailability.UNAVAILABLE),
        )
        val unavailableRepository = CoreMemoryRepository(unavailableCore, backgroundScope)
        assertEquals(
            YouSurfaceAvailability.UNAVAILABLE,
            unavailableRepository.snapshot.value.availability,
        )
        assertTrue(unavailableRepository.snapshot.value.notes.isEmpty())
    }

    @Test
    fun `an editor cannot overwrite or delete a newer Memory revision`() = runTest {
        val core = RecordingAssistantConfigurationCoreApiClient(
            memoryStatus(records = listOf(memoryRecord())),
        )
        val repository = CoreMemoryRepository(core, backgroundScope)

        val staleSave = repository.upsertYouWrote(
            id = "memory-1",
            statement = "Old draft",
            expectedMemoryRevision = 26uL,
            expectedRecordRevision = 4uL,
        )
        val staleDelete = repository.delete(
            id = "memory-1",
            expectedMemoryRevision = 27uL,
            expectedRecordRevision = 3uL,
        )

        assertEquals(TaffyResult.Failure(FailureReason.STALE_REVISION), staleSave)
        assertEquals(TaffyResult.Failure(FailureReason.STALE_REVISION), staleDelete)
        assertTrue(core.memoryUpserts.isEmpty())
        assertTrue(core.memoryDeletions.isEmpty())
    }

    @Test
    fun `only Memory revision availability or search identity invalidates projection`() {
        val baseline = memoryStatus(records = listOf(memoryRecord()))
        assertEquals(memorySnapshotVersion(baseline), memorySnapshotVersion(baseline.copy(generation = 9uL)))
        assertNotEquals(
            memorySnapshotVersion(baseline),
            memorySnapshotVersion(baseline.copy(memory = baseline.memory.copy(revision = 28uL))),
        )
        assertNotEquals(
            memorySnapshotVersion(baseline),
            memorySnapshotVersion(
                baseline.copy(
                    memory = baseline.memory.copy(
                        search = MemorySearchView("search-2", "new", 27uL, emptyList()),
                    ),
                ),
            ),
        )
    }
}

private fun memoryStatus(
    coreAvailability: CoreAvailability = CoreAvailability.READY,
    memoryAvailability: MemoryAvailability = MemoryAvailability.AVAILABLE,
    records: List<MemoryRecordView> = emptyList(),
    search: MemorySearchView? = null,
) = CoreStatus(
    availability = coreAvailability,
    generation = 3uL,
    active_tasks = emptyList(),
    auth_state = null,
    workspaces = emptyList(),
    workspace_export = null,
    asset_delivery = null,
    provider_roster = emptyList(),
    provider_probes = emptyList(),
    provider_models = emptyList(),
    assistant_configuration = AssistantConfigurationView(
        revision = 0uL,
        disabled_abilities = emptyList(),
        preset = PersonalityPresetView.CAREFUL_RESEARCHER,
        pace = 0u,
        length = 1u,
        check_in = 0u,
    ),
    library = taffy.core_api.LibraryViewState(
        availability = taffy.core_api.LibraryAvailability.AVAILABLE,
        revision = 0uL,
        entries = emptyList(),
        search = null,
        refresh_previews = emptyList(),
        refresh_results = emptyList(),
    ),
    library_export = null,
    memory = MemoryViewState(
        availability = memoryAvailability,
        revision = 27uL,
        records = records,
        search = search,
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

private fun memoryRecord() = MemoryRecordView(
    memory_id = "memory-1",
    revision = 4uL,
    statement = "Prefer concise answers.",
    source_kind = MemorySourceKind.TAFFY_SUGGESTED,
    source_task_id = "task-9",
    source_workspace = MemoryWorkspaceView("workspace-7", "Research"),
    scope_kind = MemoryScopeKind.WORKSPACE,
    scope_workspace = MemoryWorkspaceView("workspace-7", "Research"),
    sensitivity = MemorySensitivity.SENSITIVE,
    created_at_epoch_ms = 1_728_000_000_000uL,
    updated_at_epoch_ms = 1_728_086_400_000uL,
    reviewed_at_epoch_ms = 1_728_086_400_000uL,
    expires_at_epoch_ms = 1_771_200_000_000uL,
)

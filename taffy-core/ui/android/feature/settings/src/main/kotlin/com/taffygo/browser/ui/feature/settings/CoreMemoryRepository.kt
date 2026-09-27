// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import com.taffygo.browser.ui.core.api.CoreApiClient
import com.taffygo.browser.ui.core.api.submitCoreApiCommand
import com.taffygo.browser.ui.core.common.FailureReason
import com.taffygo.browser.ui.core.common.TaffyResult
import java.security.MessageDigest
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.distinctUntilChangedBy
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.flow.stateIn
import taffy.core_api.CoreAvailability
import taffy.core_api.MemoryAvailability
import taffy.core_api.MemoryRecordView
import taffy.core_api.MemoryScopeKind
import taffy.core_api.MemorySensitivity
import taffy.core_api.MemorySourceKind

/** Durable, profile-scoped Memory projected and mutated only through the core. */
internal class CoreMemoryRepository(
    private val core: CoreApiClient,
    scope: CoroutineScope,
) : MemoryRepository {
    override val snapshot: StateFlow<MemoryRepository.Snapshot> = core.status
        .distinctUntilChangedBy(::memorySnapshotVersion)
        .map(::projectMemorySnapshot)
        .stateIn(
            scope = scope,
            started = SharingStarted.Eagerly,
            initialValue = projectMemorySnapshot(core.status.value),
        )

    override suspend fun upsertYouWrote(
        id: String?,
        statement: String,
        expectedMemoryRevision: ULong,
        expectedRecordRevision: ULong,
    ): TaffyResult<Unit> {
        val trimmed = statement.trim()
        if (trimmed.isEmpty()) return TaffyResult.Failure(FailureReason.INVALID_REQUEST)
        val memory = core.status.value.memory
        if (!memoryAvailable()) return TaffyResult.Failure(FailureReason.CORE_UNAVAILABLE)
        if (memory.revision != expectedMemoryRevision) {
            return TaffyResult.Failure(FailureReason.STALE_REVISION)
        }
        val current = id?.let { candidate ->
            memory.records.firstOrNull { it.memory_id == candidate }
        }
        if (id != null && current == null) {
            return TaffyResult.Failure(FailureReason.NOT_FOUND)
        }
        if (current != null && current.revision != expectedRecordRevision) {
            return TaffyResult.Failure(FailureReason.STALE_REVISION)
        }
        if (id == null && expectedRecordRevision != 0uL) {
            return TaffyResult.Failure(FailureReason.INVALID_REQUEST)
        }
        return submitCoreApiCommand {
            core.upsertMemory(
                memoryId = current?.memory_id,
                statement = trimmed,
                scopeKind = current?.scope_kind ?: MemoryScopeKind.ALL_TASKS,
                scopeWorkspace = current?.scope_workspace,
                sensitivity = current?.sensitivity ?: MemorySensitivity.STANDARD,
                expectedMemoryRevision = expectedMemoryRevision,
                expectedRecordRevision = expectedRecordRevision,
                expiresAtEpochMillis = current?.expires_at_epoch_ms ?: 0uL,
            )
        }
    }

    override suspend fun search(query: String): TaffyResult<Unit> {
        val memory = core.status.value.memory
        if (!memoryAvailable()) return TaffyResult.Failure(FailureReason.CORE_UNAVAILABLE)
        if (query.trim().isEmpty()) return TaffyResult.Success(Unit)
        return submitCoreApiCommand {
            core.searchMemory(
                requestId = memorySearchRequestId(memory.revision, query),
                query = query,
                limit = MAX_MEMORY_SEARCH_RESULTS,
            )
        }
    }

    override suspend fun delete(
        id: String,
        expectedMemoryRevision: ULong,
        expectedRecordRevision: ULong,
    ): TaffyResult<Unit> {
        val memory = core.status.value.memory
        if (!memoryAvailable()) return TaffyResult.Failure(FailureReason.CORE_UNAVAILABLE)
        if (memory.revision != expectedMemoryRevision) {
            return TaffyResult.Failure(FailureReason.STALE_REVISION)
        }
        val current = memory.records.firstOrNull { it.memory_id == id }
            ?: return TaffyResult.Failure(FailureReason.NOT_FOUND)
        if (current.revision != expectedRecordRevision) {
            return TaffyResult.Failure(FailureReason.STALE_REVISION)
        }
        return submitCoreApiCommand {
            core.deleteMemory(current.memory_id, expectedMemoryRevision, expectedRecordRevision)
        }
    }

    private fun memoryAvailable(): Boolean {
        val status = core.status.value
        return status.availability == CoreAvailability.READY &&
            status.memory.availability == MemoryAvailability.AVAILABLE
    }
}

/** A Memory revision owns its rows; one search request identity owns its bounded result. */
internal data class MemorySnapshotVersion(
    val coreAvailability: CoreAvailability,
    val availability: MemoryAvailability,
    val revision: ULong,
    val searchRequestId: String?,
    val searchQuery: String?,
)

internal fun memorySnapshotVersion(status: taffy.core_api.CoreStatus) = MemorySnapshotVersion(
    coreAvailability = status.availability,
    availability = status.memory.availability,
    revision = status.memory.revision,
    searchRequestId = status.memory.search?.request_id,
    searchQuery = status.memory.search?.query,
)

internal fun projectMemorySnapshot(status: taffy.core_api.CoreStatus): MemoryRepository.Snapshot =
    when (status.availability) {
        CoreAvailability.STARTING -> MemoryRepository.Snapshot(YouSurfaceAvailability.LOADING)
        CoreAvailability.UNAVAILABLE,
        CoreAvailability.CIRCUIT_OPEN,
        -> MemoryRepository.Snapshot(YouSurfaceAvailability.UNAVAILABLE)
        CoreAvailability.READY -> when (status.memory.availability) {
            MemoryAvailability.AVAILABLE -> MemoryRepository.Snapshot(
                availability = YouSurfaceAvailability.READY,
                notes = status.memory.records.map(::projectMemoryNote),
                revision = status.memory.revision,
                search = status.memory.search
                    ?.takeIf { it.memory_revision == status.memory.revision }
                    ?.let { result ->
                        MemoryRepository.Search(
                            query = result.query,
                            memoryIds = result.hits.mapTo(linkedSetOf()) { it.memory_id },
                        )
                    },
            )
            MemoryAvailability.PRIVATE_PROFILE,
            MemoryAvailability.UNAVAILABLE,
            -> MemoryRepository.Snapshot(YouSurfaceAvailability.UNAVAILABLE)
        }
    }

private fun projectMemoryNote(record: MemoryRecordView): MemoryRepository.Note =
    MemoryRepository.Note(
        id = record.memory_id,
        revision = record.revision,
        statement = record.statement,
        source = when (record.source_kind) {
            MemorySourceKind.YOU_WROTE -> MemoryRepository.Source.YOU_WROTE
            MemorySourceKind.TAFFY_SUGGESTED -> MemoryRepository.Source.TAFFY_NOTICED
        },
        addedEpochDay = epochDay(record.created_at_epoch_ms),
        workspaceName = record.source_workspace?.display_name,
        sourceTaskId = record.source_task_id,
        scope = when (record.scope_kind) {
            MemoryScopeKind.ALL_TASKS -> MemoryRepository.Scope.ALL_TASKS
            MemoryScopeKind.WORKSPACE -> MemoryRepository.Scope.WORKSPACE
        },
        scopeWorkspaceName = record.scope_workspace?.display_name,
        sensitive = record.sensitivity == MemorySensitivity.SENSITIVE,
        expiresEpochDay = record.expires_at_epoch_ms
            .takeIf { it != 0uL }
            ?.let(::epochDay),
    )

private fun epochDay(epochMillis: ULong): Long = (epochMillis / MILLIS_PER_DAY).toLong()

private fun memorySearchRequestId(revision: ULong, query: String): String {
    val digest = MessageDigest.getInstance("SHA-256")
        .digest("$revision\u0000$query".encodeToByteArray())
    return buildString(78) {
        append("memory-search-")
        digest.forEach { byte ->
            val value = byte.toInt() and 0xff
            append(HEX[value ushr 4])
            append(HEX[value and 0x0f])
        }
    }
}

private const val MILLIS_PER_DAY: ULong = 86_400_000uL
private const val MAX_MEMORY_SEARCH_RESULTS: UInt = 32u
private const val HEX = "0123456789abcdef"

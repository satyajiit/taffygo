// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import com.taffygo.browser.ui.core.common.FailureReason
import com.taffygo.browser.ui.core.common.TaffyResult
import kotlinx.coroutines.flow.StateFlow

/**
 * Sentences Taffy may remember about how you like to work.
 *
 * Every row has a why-line. Nothing is inferred from private or sensitive
 * browsing. If we cannot show why Taffy knows it, it is not a row.
 */
interface MemoryRepository {

    /** Visible notes, or an honest unavailable. */
    val snapshot: StateFlow<Snapshot>

    /** Insert or replace a note you wrote, with a total boundary result. */
    suspend fun upsertYouWrote(
        id: String?,
        statement: String,
        expectedMemoryRevision: ULong,
        expectedRecordRevision: ULong,
    ): TaffyResult<Unit>

    /** Ask the core for one bounded deterministic search over visible Memory. */
    suspend fun search(query: String): TaffyResult<Unit> =
        TaffyResult.Failure(FailureReason.INVALID_REQUEST)

    /** Forget [id], or say why the mutation was not accepted. */
    suspend fun delete(
        id: String,
        expectedMemoryRevision: ULong,
        expectedRecordRevision: ULong,
    ): TaffyResult<Unit>

    /** One complete answer from the port. */
    data class Snapshot(
        val availability: YouSurfaceAvailability,
        val notes: List<Note> = emptyList(),
        val processScoped: Boolean = false,
        val revision: ULong = 0uL,
        val search: Search? = null,
    )

    /** Exact result identities from the core's latest resident revision. */
    data class Search(val query: String, val memoryIds: Set<String>)

    /** One visible sentence. */
    data class Note(
        val id: String,
        val revision: ULong = 1uL,
        val statement: String,
        val source: Source,
        val addedEpochDay: Long,
        val workspaceName: String? = null,
        val sourceTaskId: String? = null,
        val scope: Scope = Scope.ALL_TASKS,
        val scopeWorkspaceName: String? = null,
        val sensitive: Boolean = false,
        val expiresEpochDay: Long? = null,
    )

    /** Who put the row here. */
    enum class Source {
        YOU_WROTE,
        TAFFY_NOTICED,
    }

    /** Which later tasks may receive the statement. */
    enum class Scope {
        ALL_TASKS,
        WORKSPACE,
    }
}

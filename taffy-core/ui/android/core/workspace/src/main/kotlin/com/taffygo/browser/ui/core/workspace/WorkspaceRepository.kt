// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.workspace

import com.taffygo.browser.ui.core.common.TaffyResult
import com.taffygo.browser.ui.core.model.ExportFormat
import com.taffygo.browser.ui.core.model.FactId
import com.taffygo.browser.ui.core.model.SourceId
import com.taffygo.browser.ui.core.model.Workspace
import com.taffygo.browser.ui.core.model.WorkspaceExport
import com.taffygo.browser.ui.core.model.WorkspaceId
import kotlinx.coroutines.flow.StateFlow

/**
 * Where research lives after a task ends (UX spec section 7).
 *
 * A workspace keeps the state its task really reached. Nothing here promotes a
 * partly-done workspace to done, and nothing here overwrites what a page said
 * with what a user corrected — the correction sits beside it.
 */
interface WorkspaceRepository {

    /** Whether the complete saved-workspace projection is loading, ready, or unavailable. */
    val availability: StateFlow<Availability>

    /** Every workspace, most recently updated first. */
    val workspaces: StateFlow<List<Workspace>>

    /** The latest Rust-generated export completion for this profile. */
    val latestExport: StateFlow<WorkspaceExport?>

    /** One workspace, or null when the identifier is not one. */
    fun workspace(id: WorkspaceId): Workspace?

    /** Record the user's value beside what the page said. */
    suspend fun correctFact(id: WorkspaceId, factId: FactId, value: String): TaffyResult<Unit>

    /** Remove a source, and mark every cell that rested only on it. */
    suspend fun excludeSource(id: WorkspaceId, sourceId: SourceId): TaffyResult<Unit>

    /** Return an exact cached Rust export for the workspace's current revision. */
    fun renderExport(id: WorkspaceId, format: ExportFormat): String?

    /** Ask Rust to render an export; Android never formats workspace content. */
    suspend fun requestExport(id: WorkspaceId, format: ExportFormat): TaffyResult<Unit>

    /** Change one saved workspace display name at the exact revision shown. */
    suspend fun rename(
        id: WorkspaceId,
        expectedRevision: ULong,
        displayName: String,
    ): TaffyResult<Unit>

    /** Delete one saved workspace with its exact published confirmation challenge. */
    suspend fun delete(
        id: WorkspaceId,
        expectedRevision: ULong,
        confirmationToken: String,
    ): TaffyResult<Unit>

    enum class Availability { LOADING, READY, UNAVAILABLE }
}

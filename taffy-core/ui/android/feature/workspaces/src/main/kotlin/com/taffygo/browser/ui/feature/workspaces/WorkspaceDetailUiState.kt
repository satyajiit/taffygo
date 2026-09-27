// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.workspaces

import com.taffygo.browser.ui.core.model.Fact
import com.taffygo.browser.ui.core.model.SourceRecord
import com.taffygo.browser.ui.core.model.TaskDisplayState
import com.taffygo.browser.ui.core.model.WorkspaceId
import com.taffygo.browser.ui.core.model.WorkspaceDeletionPreview

/**
 * Screen SCR-305 — one workspace: its output, its sources, its conflicts.
 *
 * [missing] is a state of its own rather than an empty list, because a
 * workspace identifier that no longer resolves is a different thing from a
 * workspace with nothing in it.
 */
data class WorkspaceDetailUiState(
    /** Which workspace, when there is one. */
    val id: WorkspaceId? = null,
    /** Exact saved-workspace revision rendered by this state. */
    val revision: ULong = 0uL,
    /** Mutable display name shown as the screen title. */
    val displayName: String = "",
    /** The goal in the user's own words. */
    val goal: String = "",
    /** The state the workspace really reached. */
    val state: TaskDisplayState = TaskDisplayState.STOPPED,
    /** Every fact, grouped by field in the screen. */
    val facts: List<Fact> = emptyList(),
    /** Every source, including the excluded ones. */
    val sources: List<SourceRecord> = emptyList(),
    /** Exact content-free challenge for destructive confirmation. */
    val deletionPreview: WorkspaceDeletionPreview? = null,
    /**
     * True until the repository has published this identifier. A first frame
     * with no workspace is this, not [missing].
     */
    val loading: Boolean = false,
    /** Whether the complete workspace projection is unavailable. */
    val unavailable: Boolean = false,
    /** Whether the identifier resolved at all. */
    val missing: Boolean = false,
    /** Whether this exact saved-workspace fact may be explicitly kept. */
    val canKeepFacts: Boolean = false,
) {
    private val factCounts: FactCounts by lazy(LazyThreadSafetyMode.NONE) {
        var conflicts = 0
        var needsANewSource = 0
        for (fact in facts) {
            if (fact.hasConflict) conflicts += 1
            if (fact.needsANewSource) needsANewSource += 1
        }
        FactCounts(conflicts = conflicts, needsANewSource = needsANewSource)
    }

    /** How many facts two sources disagree about. */
    val conflictCount: Int
        get() = factCounts.conflicts

    /** How many cells lost the only source that supported them. */
    val needsANewSourceCount: Int
        get() = factCounts.needsANewSource

    private data class FactCounts(
        val conflicts: Int,
        val needsANewSource: Int,
    )
}

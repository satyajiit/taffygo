// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.model

/**
 * Where research lives (UX spec section 7): the goal in the user's own words,
 * the state the task really reached, and the sources it rests on.
 *
 * A saved partly-done workspace stays partly done. Nothing here rounds partial
 * work up to success.
 */
data class Workspace(
    /** Identity of the workspace. */
    val id: WorkspaceId,
    /** The goal, in the user's own words. */
    val goal: String,
    /** The state the task reached, kept honestly. */
    val state: TaskDisplayState,
    /** When the workspace last changed, in epoch milliseconds. */
    val lastUpdatedEpochMillis: Long,
    /** The output shape this workspace builds. */
    val template: TaskTemplate,
    /** Every page the task read. */
    val sources: List<SourceRecord>,
    /** Every fact the output holds. */
    val facts: List<Fact>,
    /** Exact canonical revision observed by this projection. */
    val revision: ULong = 0uL,
    /** Mutable display name; [goal] remains the immutable task request. */
    val displayName: String = goal,
    /** Exact content-free deletion challenge, present only for saved workspaces. */
    val deletionPreview: WorkspaceDeletionPreview? = null,
) {
    /** How many facts two sources disagree about. */
    val conflictCount: Int
        get() = facts.count { it.hasConflict }

    /** How many sources are still in scope. */
    val activeSourceCount: Int
        get() = sources.count { !it.excluded }
}

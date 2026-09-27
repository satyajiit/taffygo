// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.workspaces

import com.taffygo.browser.ui.core.model.Workspace

/** What screen SCR-305 shows, or the missing state when nothing resolved. */
internal fun projectWorkspaceDetail(
    workspace: Workspace?,
    loading: Boolean = false,
    unavailable: Boolean = false,
    canKeepFacts: Boolean = false,
): WorkspaceDetailUiState =
    when {
        workspace != null -> WorkspaceDetailUiState(
            id = workspace.id,
            revision = workspace.revision,
            displayName = workspace.displayName,
            goal = workspace.goal,
            state = workspace.state,
            // Grouped by field so a comparison reads down a column, and stable
            // within a field so the order never depends on insertion.
            facts = workspace.facts.sortedWith(compareBy({ it.field }, { it.id.value })),
            sources = workspace.sources.sortedBy { it.host },
            deletionPreview = workspace.deletionPreview,
            canKeepFacts = canKeepFacts,
        )
        loading -> WorkspaceDetailUiState(loading = true)
        unavailable -> WorkspaceDetailUiState(unavailable = true)
        else -> WorkspaceDetailUiState(missing = true)
    }

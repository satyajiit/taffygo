// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.workspaces

import com.taffygo.browser.ui.core.browser.BrowserProfilesRepository
import com.taffygo.browser.ui.core.model.Workspace

/**
 * What screen SCR-304 shows.
 *
 * The search matches the goal and the hosts of the sources, because those are
 * the two things a person remembers about a piece of research. Order is
 * most-recently-updated first and never anything else.
 */
internal fun projectWorkspaceList(
    workspaces: List<Workspace>,
    query: String,
    loading: Boolean = false,
    unavailable: Boolean = false,
    profileName: String? = null,
): WorkspaceListUiState {
    val trimmed = query.trim()
    val matched = workspaces
        .filter { workspace ->
            trimmed.isEmpty() ||
                workspace.displayName.contains(trimmed, ignoreCase = true) ||
                workspace.goal.contains(trimmed, ignoreCase = true) ||
                workspace.sources.any { it.host.contains(trimmed, ignoreCase = true) }
        }
        .sortedByDescending { it.lastUpdatedEpochMillis }
    return WorkspaceListUiState(
        workspaces = matched,
        query = query,
        totalCount = workspaces.size,
        loading = loading,
        unavailable = unavailable,
        profileName = profileName,
    )
}

/**
 * The name screen SCR-304 puts under its title.
 *
 * A device with one profile has nothing to tell apart, so the line is absent
 * rather than stating the obvious. It appears once a second profile exists,
 * because from then on switching profiles swaps the whole list (decision 0102).
 */
internal fun workspaceProfileName(snapshot: BrowserProfilesRepository.Snapshot): String? {
    if (snapshot.availability != BrowserProfilesRepository.Availability.READY) return null
    if (snapshot.profiles.size < 2) return null
    return snapshot.profiles.singleOrNull { it.active }?.displayName?.takeIf { it.isNotBlank() }
}

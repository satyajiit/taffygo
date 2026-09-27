// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import com.taffygo.browser.ui.core.browser.BrowserProfilesRepository

/** Local, synchronous transitions for SCR-708. Repository work stays in its view model. */
internal fun reduceBrowserProfiles(
    state: BrowserProfilesUiState,
    intent: BrowserProfilesIntent,
): BrowserProfilesUiState = when (intent) {
    BrowserProfilesIntent.Refresh -> state.copy(failure = null)
    is BrowserProfilesIntent.AskToDelete -> if (state.busy) {
        state
    } else {
        state.copy(
            deleteCandidate = state.profiles.firstOrNull {
                it.id == intent.profileId && !it.active
            },
            failure = null,
        )
    }
    BrowserProfilesIntent.DismissDelete -> if (state.busy) {
        state
    } else {
        state.copy(deleteCandidate = null)
    }
    BrowserProfilesIntent.ConfirmDelete -> state
    BrowserProfilesIntent.DismissFailure -> state.copy(failure = null)
}

internal fun projectBrowserProfiles(
    snapshot: BrowserProfilesRepository.Snapshot,
    local: BrowserProfilesUiState,
    activeWorkspaceCount: Int? = null,
): BrowserProfilesUiState {
    val candidates = if (snapshot.availability == BrowserProfilesRepository.Availability.READY) {
        snapshot.profiles
            .filter { it.id.isNotBlank() && it.displayName.isNotBlank() }
    } else {
        emptyList()
    }
    val valid = candidates.isNotEmpty() &&
        candidates.count(BrowserProfilesRepository.Profile::active) == 1 &&
        candidates.map(BrowserProfilesRepository.Profile::id).distinct().size == candidates.size
    val availability = if (
        snapshot.availability == BrowserProfilesRepository.Availability.READY && !valid
    ) {
        BrowserProfilesRepository.Availability.UNAVAILABLE
    } else {
        snapshot.availability
    }
    val profiles = if (availability == BrowserProfilesRepository.Availability.READY) {
        candidates
    } else {
        emptyList()
    }
    return local.copy(
        availability = availability,
        profiles = profiles,
        activeWorkspaceCount = activeWorkspaceCount,
        deleteCandidate = local.deleteCandidate?.id?.let { id ->
            profiles.firstOrNull { it.id == id && !it.active }
        },
    )
}

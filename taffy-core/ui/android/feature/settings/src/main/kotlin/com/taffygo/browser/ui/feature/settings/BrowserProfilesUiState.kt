// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import com.taffygo.browser.ui.core.browser.BrowserProfilesRepository

/** Screen SCR-708's immutable projection. */
data class BrowserProfilesUiState(
    val availability: BrowserProfilesRepository.Availability =
        BrowserProfilesRepository.Availability.LOADING,
    val profiles: List<BrowserProfilesRepository.Profile> = emptyList(),
    val deleteCandidate: BrowserProfilesRepository.Profile? = null,
    val operation: Operation? = null,
    val failure: BrowserProfilesRepository.Failure? = null,
    /**
     * How many workspaces the current profile holds, or null until the
     * workspace list has loaded. Other profiles' journals are not open, so no
     * count exists for them (decision 0102).
     */
    val activeWorkspaceCount: Int? = null,
) {
    val busy: Boolean
        get() = operation != null

    enum class Operation {
        DELETING,
    }
}

// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

/** Screen SCR-205 — global defaults followed by per-site exceptions. */
data class SiteSettingsUiState(
    val loading: Boolean = false,
    val available: Boolean = true,
    val defaults: List<Default> = emptyList(),
    val sites: List<Site> = emptyList(),
    val updating: SiteSettingsRepository.Capability? = null,
    val failedUpdate: SiteSettingsRepository.Capability? = null,
    val resetCandidate: String? = null,
    val resettingHost: String? = null,
    val failedResetHost: String? = null,
) {
    data class Default(
        val capability: SiteSettingsRepository.Capability,
        val enabled: Boolean,
        val userModifiable: Boolean,
    )

    data class Site(
        val host: String,
        val changedPermissionCount: Int,
        val changedCapabilities: Set<SiteSettingsRepository.Capability> = emptySet(),
    )
}

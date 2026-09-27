// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

internal fun reduceSiteSettings(
    state: SiteSettingsUiState,
    intent: SiteSettingsIntent,
): SiteSettingsUiState = when (intent) {
    is SiteSettingsIntent.SetDefault -> {
        val setting = state.defaults.firstOrNull { it.capability == intent.capability }
        if (
            state.updating == null &&
            setting?.userModifiable == true &&
            setting.enabled != intent.enabled
        ) {
            state.copy(updating = intent.capability, failedUpdate = null)
        } else {
            state
        }
    }
    is SiteSettingsIntent.RequestSiteReset -> if (
        state.updating == null &&
        state.resettingHost == null &&
        state.sites.any { it.host == intent.host }
    ) {
        state.copy(resetCandidate = intent.host, failedResetHost = null)
    } else {
        state
    }
    SiteSettingsIntent.DismissSiteReset -> if (state.resettingHost == null) {
        state.copy(resetCandidate = null)
    } else {
        state
    }
    SiteSettingsIntent.ConfirmSiteReset -> state.resetCandidate?.let { host ->
        if (state.resettingHost == null && state.updating == null) {
            state.copy(
                resetCandidate = null,
                resettingHost = host,
                failedResetHost = null,
            )
        } else {
            state
        }
    } ?: state
    SiteSettingsIntent.Dismiss -> if (state.resettingHost == null) {
        state.copy(resetCandidate = null)
    } else {
        state
    }
    SiteSettingsIntent.Retry -> state
}

internal fun projectSiteSettings(
    snapshot: SiteSettingsRepository.Snapshot,
): SiteSettingsUiState = when (snapshot) {
    SiteSettingsRepository.Snapshot.Loading -> SiteSettingsUiState(loading = true)
    SiteSettingsRepository.Snapshot.Unavailable -> SiteSettingsUiState(available = false)
    is SiteSettingsRepository.Snapshot.Ready -> SiteSettingsUiState(
        defaults = snapshot.defaults.map {
            SiteSettingsUiState.Default(it.capability, it.enabled, it.userModifiable)
        },
        sites = snapshot.sites.map {
            SiteSettingsUiState.Site(
                it.host,
                it.changedPermissionCount,
                it.changedCapabilities,
            )
        },
    )
}

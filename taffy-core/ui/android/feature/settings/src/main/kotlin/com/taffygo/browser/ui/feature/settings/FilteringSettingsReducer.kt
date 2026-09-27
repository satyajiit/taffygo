// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import com.taffygo.browser.ui.core.browser.FilteringSettings

/**
 * The projection from browser truth to what screen SCR-206 renders. Pure, so
 * every state — toggle off, an empty exception list, a missing week — is a
 * plain unit test.
 */
internal fun projectFilteringSettings(
    settings: FilteringSettings,
    week: BlockingWeekRepository.Snapshot = BlockingWeekRepository.Snapshot(),
    removal: FilteringSettingsUiState.Removal = FilteringSettingsUiState.Removal(),
): FilteringSettingsUiState = FilteringSettingsUiState(
    enabled = settings.enabled,
    blockedTotal = settings.blockedTotal,
    blockedThisWeek = week.blockedThisWeek,
    minimumSitesThisWeek = week.minimumSitesThisWeek,
    exceptionHosts = settings.exceptionHosts,
    // A result about a host that has since left the list is a result about
    // nothing, so it is dropped here rather than drawn against another row.
    removal = removal.takeIf {
        it.host.isBlank() || settings.exceptionHosts.contains(it.host)
    } ?: FilteringSettingsUiState.Removal(),
)

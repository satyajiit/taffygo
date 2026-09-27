// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import com.taffygo.browser.ui.core.browser.FilteringSettings
import com.taffygo.browser.ui.core.browser.NavigationState

/**
 * The projection from browser truth to what the site sheet renders.
 *
 * Pure, so every state of screen SCR-204 — active, excepted, master toggle
 * off, a tab that has been nowhere — is a plain unit test.
 *
 * Whether this site is excepted is read from [NavigationState], not worked out
 * here. This file used to carry its own copy of the browser's subdomain rule,
 * and a second copy is a second answer that can drift — it also could not see
 * which profile applied, which is the defect decision 0128 was written for.
 * The rule lives in `taffy-core/components/filtering/core/posture.cc` and is
 * pinned by `posture_unittest.cc`, which means it is covered by
 * `taffy_unittests` and not by `./tools/check fast`.
 */
internal fun projectSiteFiltering(
    navigation: NavigationState,
    settings: FilteringSettings,
    desktopSite: Boolean = false,
    desktopSiteAvailable: Boolean = false,
    permissions: SiteInfoRepository.PermissionState =
        SiteInfoRepository.PermissionState.Unavailable,
    permissionReset: SiteFilteringUiState.ActionProgress =
        SiteFilteringUiState.ActionProgress.IDLE,
    permissionResetConfirmation: Boolean = false,
    siteBlocking: SiteFilteringUiState.ActionProgress =
        SiteFilteringUiState.ActionProgress.IDLE,
    pageZoom: PageZoomState = PageZoomState(),
): SiteFilteringUiState = SiteFilteringUiState(
    host = navigation.host,
    filteringActive = navigation.filteringActive,
    blockedCount = navigation.blockedRequestCount,
    enabled = settings.enabled,
    excepted = navigation.siteExcepted,
    isSecure = navigation.isSecure,
    desktopSite = desktopSite,
    desktopSiteAvailable = desktopSiteAvailable,
    permissions = permissions,
    permissionReset = permissionReset,
    permissionResetConfirmation = permissionResetConfirmation,
    siteBlocking = siteBlocking,
    pageZoom = pageZoom,
)

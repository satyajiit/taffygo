// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

/** Everything screen SCR-205 can be asked to do. */
sealed interface SiteSettingsIntent {
    data class SetDefault(
        val capability: SiteSettingsRepository.Capability,
        val enabled: Boolean,
    ) : SiteSettingsIntent

    data class RequestSiteReset(val host: String) : SiteSettingsIntent
    data object DismissSiteReset : SiteSettingsIntent
    data object ConfirmSiteReset : SiteSettingsIntent
    data object Retry : SiteSettingsIntent
    data object Dismiss : SiteSettingsIntent
}

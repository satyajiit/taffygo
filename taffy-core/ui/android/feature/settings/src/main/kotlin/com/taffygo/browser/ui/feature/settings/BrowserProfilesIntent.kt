// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

/**
 * Everything screen SCR-708 can be asked to do.
 *
 * There is no create and no switch: 1.0 ships one browser profile (decision
 * 0255), because Chromium on Android builds startup data for the first profile
 * only and a second one aborts the browser. Delete stays for a profile an
 * earlier build left behind; it is reachable only when one is listed.
 */
sealed interface BrowserProfilesIntent {
    data object Refresh : BrowserProfilesIntent

    data class AskToDelete(val profileId: String) : BrowserProfilesIntent

    data object ConfirmDelete : BrowserProfilesIntent

    data object DismissDelete : BrowserProfilesIntent

    data object DismissFailure : BrowserProfilesIntent
}

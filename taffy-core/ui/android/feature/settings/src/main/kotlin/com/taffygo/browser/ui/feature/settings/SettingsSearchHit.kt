// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import com.taffygo.browser.ui.core.ui.TaffyDestination

/**
 * One settings-search result: where it goes, and the words the row shows.
 *
 * Hits include home rows and search-only leaves (You, Memory, saved sign-ins,
 * skills, Library). They never invent a History row — overflow owns History.
 */
data class SettingsSearchHit(
    val destination: TaffyDestination,
    val title: String,
    val summary: String,
    val section: SettingsSection,
)

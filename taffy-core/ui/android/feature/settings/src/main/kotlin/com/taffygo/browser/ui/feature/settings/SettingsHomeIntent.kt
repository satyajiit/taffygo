// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import com.taffygo.browser.ui.core.ui.TaffyDestination

/** Everything screen SCR-401 can be asked to do. */
sealed interface SettingsHomeIntent {

    /** The user typed in the search box. */
    data class QueryChanged(val query: String) : SettingsHomeIntent

    /** Open or close the toolbar search field. Closing clears the query. */
    data object ToggleSearch : SettingsHomeIntent

    /** Open a search hit or home row, including You from the identity card. */
    data class Open(val destination: TaffyDestination) : SettingsHomeIntent
}

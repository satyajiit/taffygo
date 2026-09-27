// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

/** Everything screen SCR-206 can be asked to do. */
sealed interface FilteringSettingsIntent {

    /** Turn blocking on or off for the whole profile. */
    data class SetEnabled(val enabled: Boolean) : FilteringSettingsIntent

    /** Turn blocking back on for one excepted site. */
    data class RemoveException(val host: String) : FilteringSettingsIntent
}

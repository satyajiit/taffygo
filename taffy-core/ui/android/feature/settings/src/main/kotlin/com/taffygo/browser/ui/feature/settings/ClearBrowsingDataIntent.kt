// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

/** Everything screen SCR-207 can be asked to do. */
sealed interface ClearBrowsingDataIntent {
    data class SelectRange(val range: ClearBrowsingDataUiState.Range) : ClearBrowsingDataIntent
    data class ToggleClass(
        val dataClass: ClearBrowsingDataUiState.DataClass,
    ) : ClearBrowsingDataIntent
    data object Confirm : ClearBrowsingDataIntent
    data object DismissConfirm : ClearBrowsingDataIntent
    data object Submit : ClearBrowsingDataIntent
    data object Dismiss : ClearBrowsingDataIntent
}

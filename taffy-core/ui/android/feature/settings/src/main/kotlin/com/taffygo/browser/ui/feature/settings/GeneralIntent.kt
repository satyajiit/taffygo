// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

/** Everything screen SCR-402 can be asked to do. */
sealed interface GeneralIntent {
    data object ChooseSearchEngine : GeneralIntent
    data object DismissSearchEngine : GeneralIntent
    data object ChooseDownloadLocation : GeneralIntent
    data class SelectDownloadLocation(val id: String) : GeneralIntent
    data object DismissDownloadLocation : GeneralIntent
    data object OpenAppearance : GeneralIntent
    data object Dismiss : GeneralIntent
}

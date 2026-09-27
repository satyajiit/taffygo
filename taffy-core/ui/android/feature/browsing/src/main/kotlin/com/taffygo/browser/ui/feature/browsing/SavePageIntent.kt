// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

/** Everything the save-page sheet can be asked to do. */
sealed interface SavePageIntent {

    /** Pick a folder. Empty means the default "All". */
    data class ChooseFolder(val folderId: String) : SavePageIntent

    /** Star the page. No-op when [SavePageUiState.primaryEnabled] is false. */
    data object Save : SavePageIntent

    /** Close the sheet without saving. */
    data object Dismiss : SavePageIntent
}

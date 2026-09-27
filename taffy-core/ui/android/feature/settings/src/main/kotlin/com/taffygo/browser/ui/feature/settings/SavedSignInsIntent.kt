// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

/** Everything screen SCR-413 can be asked to do. No intent can request a password. */
sealed interface SavedSignInsIntent {
    data class QueryChanged(val query: String) : SavedSignInsIntent
    data class Open(val id: String) : SavedSignInsIntent
    data object DismissDetail : SavedSignInsIntent
    data object Delete : SavedSignInsIntent
    data object ConfirmDelete : SavedSignInsIntent
    data object CancelDelete : SavedSignInsIntent
}

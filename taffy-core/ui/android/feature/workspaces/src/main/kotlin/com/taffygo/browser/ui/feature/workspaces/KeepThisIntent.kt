// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.workspaces

/** Everything screen SCR-504 can be asked to do. */
sealed interface KeepThisIntent {

    /** Choose the collection this would land in. */
    data class SelectCollection(val collectionId: String) : KeepThisIntent

    /** Choose what would be kept. */
    data class SelectKind(val kind: KeepThisKind) : KeepThisIntent

    /** Ask the store to keep it. Refused while the port cannot. */
    data object Keep : KeepThisIntent

    /** Leave without keeping anything. */
    data object Dismiss : KeepThisIntent
}

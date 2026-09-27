// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.workspaces

/** Everything screen SCR-307 can be asked to do. */
sealed interface FactCorrectionIntent {

    /** The user typed a value. */
    data class ValueChanged(val value: String) : FactCorrectionIntent

    /** Record the user's value beside what the page said. */
    data object Save : FactCorrectionIntent

    /** Leave without recording anything. */
    data object Cancel : FactCorrectionIntent
}

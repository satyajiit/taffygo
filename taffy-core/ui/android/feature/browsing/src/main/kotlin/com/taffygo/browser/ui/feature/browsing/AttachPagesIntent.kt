// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import com.taffygo.browser.ui.core.model.TabId

/** Everything the Add pages sheet can be asked to do. */
sealed interface AttachPagesIntent {

    /** Narrow the open-tab list by title or host. */
    data class SearchChanged(val query: String) : AttachPagesIntent

    /** Tick or untick one open tab. */
    data class Toggle(val tabId: TabId) : AttachPagesIntent

    /** Copy the ticked set onto the composer or preview. */
    data object Confirm : AttachPagesIntent

    /** Leave the sheet without changing the list. */
    data object Dismiss : AttachPagesIntent
}

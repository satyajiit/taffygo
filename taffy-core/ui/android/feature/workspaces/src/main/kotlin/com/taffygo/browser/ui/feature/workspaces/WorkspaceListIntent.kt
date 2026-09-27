// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.workspaces

import com.taffygo.browser.ui.core.model.WorkspaceId

/** Everything screen SCR-304 can be asked to do. */
sealed interface WorkspaceListIntent {

    /** The user typed in the search box. */
    data class QueryChanged(val query: String) : WorkspaceListIntent

    /** Open one workspace. */
    data class Open(val id: WorkspaceId) : WorkspaceListIntent

    /** Start a new workspace: the Ask sheet, with the source-table shape stated, is the way in. */
    data object Create : WorkspaceListIntent
}

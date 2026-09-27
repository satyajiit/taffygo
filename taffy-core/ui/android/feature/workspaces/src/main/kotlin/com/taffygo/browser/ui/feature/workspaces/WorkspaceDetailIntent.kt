// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.workspaces

import com.taffygo.browser.ui.core.model.FactId
import com.taffygo.browser.ui.core.model.SourceId

/** Everything screen SCR-305 can be asked to do. */
sealed interface WorkspaceDetailIntent {

    /** Open the sheet that records the user's value beside the page's. */
    data class CorrectFact(val factId: FactId) : WorkspaceDetailIntent

    /** Open the page one fact came from. */
    data class OpenSource(val sourceId: SourceId) : WorkspaceDetailIntent

    /** Remove a source, and mark every cell that rested only on it. */
    data class ExcludeSource(val sourceId: SourceId) : WorkspaceDetailIntent

    /** Explicitly keep one cited fact from the exact saved revision on screen. */
    data class KeepFact(val factId: FactId) : WorkspaceDetailIntent

    /** Open the export sheet. */
    data object Export : WorkspaceDetailIntent

    /** Change only the display name at the exact revision the dialog opened on. */
    data class Rename(val expectedRevision: ULong, val displayName: String) : WorkspaceDetailIntent

    /** Delete using the exact revision and challenge the dialog rendered. */
    data class Delete(
        val expectedRevision: ULong,
        val confirmationToken: String,
    ) : WorkspaceDetailIntent
}

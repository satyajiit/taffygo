// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.workspaces

import com.taffygo.browser.ui.core.model.SourceId
import com.taffygo.browser.ui.core.model.Workspace

/** What screen SCR-306 shows, or the missing state when nothing resolved. */
internal fun projectSourceViewer(
    workspace: Workspace?,
    sourceId: SourceId,
    loading: Boolean = false,
    unavailable: Boolean = false,
): SourceViewerUiState {
    val source = workspace?.sources?.firstOrNull { it.id == sourceId }
    return when {
        source != null -> SourceViewerUiState(
            title = source.title,
            host = source.host,
            readAtEpochMillis = source.readAtEpochMillis,
            factCount = source.factCount,
            excluded = source.excluded,
        )
        loading -> SourceViewerUiState(loading = true)
        unavailable -> SourceViewerUiState(unavailable = true)
        else -> SourceViewerUiState(missing = true)
    }
}

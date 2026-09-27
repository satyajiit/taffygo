// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.workspaces

/** Everything screen SCR-306 can be asked to do. */
sealed interface SourceViewerIntent {

    /** Open the live page in a tab. */
    data object OpenLivePage : SourceViewerIntent

    /** Go back to the workspace. */
    data object Close : SourceViewerIntent
}

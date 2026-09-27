// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

/** Everything screen SCR-411 can be asked to do. */
sealed interface TimeOnSitesIntent {

    /** Switch Today / This week. */
    data class SelectRange(val range: TimeOnSitesUiState.Range) : TimeOnSitesIntent

    /**
     * A site row was tapped. This swarm does not open site info from here —
     * that would reach browsing types this module must not depend on.
     */
    data class OpenSite(val site: String) : TimeOnSitesIntent
}

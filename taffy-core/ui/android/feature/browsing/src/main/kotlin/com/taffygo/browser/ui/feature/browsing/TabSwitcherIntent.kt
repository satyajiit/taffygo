// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import com.taffygo.browser.ui.core.model.TabId

/** Everything screen SCR-104 can be asked to do. */
sealed interface TabSwitcherIntent {

    /** Show this tab. */
    data class Select(val id: TabId) : TabSwitcherIntent

    /** Close this tab. */
    data class Close(val id: TabId) : TabSwitcherIntent

    /** Show the user's own tabs, or the private ones. */
    data class SelectGroup(val group: TabSwitcherGroup) : TabSwitcherIntent

    /** Show or hide Taffy's own group. */
    data object ToggleTaffyGroup : TabSwitcherIntent

    /** Open a new tab. */
    data object NewTab : TabSwitcherIntent

    /** Begin turning what is open into a workspace. */
    data object StartWorkspace : TabSwitcherIntent

    /** Narrow the grid to tabs whose title or host contains this. */
    data class Search(val query: String) : TabSwitcherIntent

    /** Ask to close every tab in the segment showing. */
    data object RequestCloseVisible : TabSwitcherIntent

    /** Close them. */
    data object ConfirmCloseVisible : TabSwitcherIntent

    /** Leave them open. */
    data object DismissCloseVisible : TabSwitcherIntent

    /** Enter multi-select with this card, if it can be asked about. */
    data class LongPress(val id: TabId) : TabSwitcherIntent

    /** Toggle this card in the Ask Taffy selection. */
    data class ToggleSelected(val id: TabId) : TabSwitcherIntent

    /** Ask Taffy about the selected tabs, or the active one. */
    data object AskTaffy : TabSwitcherIntent

    /** Leave multi-select without asking. */
    data object ClearSelection : TabSwitcherIntent
}

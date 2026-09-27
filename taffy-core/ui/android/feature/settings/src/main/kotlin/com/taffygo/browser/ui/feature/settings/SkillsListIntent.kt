// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

/** Everything screen SCR-601 can be asked to do. */
sealed interface SkillsListIntent {

    /** The user typed in the search box. */
    data class QueryChanged(val query: String) : SkillsListIntent

    /** Turn one ability on or off. */
    data class Toggle(val id: String) : SkillsListIntent

    /** Open one ability's details. */
    data class Open(val id: String) : SkillsListIntent
}

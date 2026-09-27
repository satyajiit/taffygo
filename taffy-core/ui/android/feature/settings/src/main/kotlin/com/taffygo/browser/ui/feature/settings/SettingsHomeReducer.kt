// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import com.taffygo.browser.ui.core.model.LocalProfile

/** What typing and opening do to screen SCR-401. */
internal fun reduceSettingsHome(
    state: SettingsHomeUiState,
    intent: SettingsHomeIntent,
): SettingsHomeUiState = when (intent) {
    is SettingsHomeIntent.QueryChanged -> state.copy(
        query = intent.query,
        searchOpen = true,
    )
    SettingsHomeIntent.ToggleSearch -> if (state.searchOpen || state.query.isNotEmpty()) {
        state.copy(searchOpen = false, query = "")
    } else {
        state.copy(searchOpen = true)
    }
    is SettingsHomeIntent.Open -> state
}

/**
 * Rebuild the local half after process death. A stored query with no
 * [searchOpen] flag still opens the field, so the trailing X closes it.
 */
internal fun restoreSettingsHome(
    query: String,
    searchOpen: Boolean?,
): SettingsHomeUiState = SettingsHomeUiState(
    query = query,
    searchOpen = searchOpen ?: query.isNotEmpty(),
)

/**
 * Identity and the week count come from their ports. The query stays
 * whatever the user typed. A profile with no name set is the ordinary
 * state, and the doorway names the screen rather than inventing a person.
 */
internal fun projectSettingsHome(
    local: SettingsHomeUiState,
    week: BlockingWeekRepository.Snapshot,
    profile: LocalProfile,
): SettingsHomeUiState = local.copy(
    blockedThisWeek = week.blockedThisWeek,
    displayName = profile.displayName,
    avatar = profile.avatar,
    monogram = profile.monogram,
)

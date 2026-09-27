// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import com.taffygo.browser.ui.core.model.LocalAvatar
import com.taffygo.browser.ui.core.model.LocalProfile

/** Screen SCR-401 — the settings home. */
data class SettingsHomeUiState(
    /** The sections that exist, in the order they are shown. */
    val sections: List<SettingsSection> = SettingsSection.entries,
    /** What the user is searching for. */
    val query: String = "",
    /** Whether the toolbar search field is open. */
    val searchOpen: Boolean = false,
    /** The name this phone holds, from `LocalProfile`. Null when unset. */
    val displayName: String? = null,
    /** The face this phone's profile wears, and the letters behind it. */
    val avatar: LocalAvatar = LocalAvatar.Monogram,
    val monogram: String = LocalProfile.MONOGRAM_FALLBACK,
    /**
     * Requests blocked this week, when the plane counted a week. Null means
     * not measured — never a lifetime total divided by seven.
     */
    val blockedThisWeek: Long? = null,
) {
    /** True once the query has a non-space character. */
    val searching: Boolean
        get() = query.trim().isNotEmpty()

    /** The sections whose localized title, summary, or synonyms match the query. */
    fun matching(searchTextOf: (SettingsSection) -> Iterable<String>): List<SettingsSection> =
        SettingsSearchIndex.matching(query, sections, searchTextOf)

    /** Settings is always a list. The grid clipped titles on the phone. */
    fun showsCategoryGrid(compact: Boolean): Boolean = false
}

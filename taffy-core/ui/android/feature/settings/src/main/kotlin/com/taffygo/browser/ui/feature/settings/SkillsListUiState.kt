// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

/** Screen SCR-601 — installed abilities, not a store. */
data class SkillsListUiState(
    val availability: SkillsRepository.Availability = SkillsRepository.Availability.LOADING,
    val query: String = "",
    val showSearch: Boolean = false,
    val groups: List<Group> = emptyList(),
    val showNoExtras: Boolean = false,
    val siteSkillsAvailable: Boolean = true,
) {
    /** One job heading and the abilities under it. */
    data class Group(
        val group: SkillsRepository.Group,
        val skills: List<Row>,
    )

    /** One installed ability in the list. */
    data class Row(
        val id: String,
        val name: String = id,
        val origin: String? = null,
        val stepCount: UInt = 0u,
        val enabled: Boolean,
        val needsRecordedReview: Boolean = false,
        val readiness: SkillsRepository.Readiness = SkillsRepository.Readiness.READY,
    )

    /**
     * Groups whose title, name or summary contains [query], using the
     * localized strings the screen already has.
     */
    fun matching(searchTextOf: (String) -> List<String>): List<Group> {
        val needle = query.trim()
        if (needle.isEmpty()) return groups
        return groups.mapNotNull { group ->
            val rows = group.skills.filter { row ->
                searchTextOf(row.id).any { it.contains(needle, ignoreCase = true) }
            }
            if (rows.isEmpty()) null else group.copy(skills = rows)
        }
    }
}

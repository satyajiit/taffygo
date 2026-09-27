// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

/** Search is local; toggle and open leave the list to the repository and navigator. */
internal fun reduceSkillsList(
    state: SkillsListUiState,
    intent: SkillsListIntent,
): SkillsListUiState = when (intent) {
    is SkillsListIntent.QueryChanged -> state.copy(query = intent.query)
    is SkillsListIntent.Toggle,
    is SkillsListIntent.Open,
    -> state
}

internal const val SKILLS_SEARCH_THRESHOLD: Int = 8

/** The projection from the skills port to screen SCR-601. */
internal fun projectSkillsList(
    snapshot: SkillsRepository.Snapshot,
    query: String,
): SkillsListUiState {
    val ready = snapshot.availability == SkillsRepository.Availability.READY
    val rowsByGroup = SkillsRepository.Group.entries.associateWith {
        ArrayList<SkillsListUiState.Row>()
    }
    var hasAddedAbility = false
    if (ready) {
        for (skill in snapshot.skills) {
            rowsByGroup.getValue(skill.group) += SkillsListUiState.Row(
                id = skill.id,
                name = plainSkillName(skill.id),
                origin = skill.origin,
                stepCount = skill.stepCount,
                enabled = skill.enabled,
                needsRecordedReview = skill.needsRecordedReview,
                readiness = skill.readiness,
            )
            hasAddedAbility = hasAddedAbility || !skill.builtIn
        }
    }
    val groups = SkillsRepository.Group.entries.mapNotNull { group ->
        rowsByGroup.getValue(group)
            .takeIf { it.isNotEmpty() }
            ?.let { SkillsListUiState.Group(group, it) }
    }
    return SkillsListUiState(
        availability = snapshot.availability,
        query = query,
        showSearch = ready && snapshot.skills.size > SKILLS_SEARCH_THRESHOLD,
        groups = groups,
        showNoExtras = ready && snapshot.siteSkillsAvailable && !hasAddedAbility,
        siteSkillsAvailable = snapshot.siteSkillsAvailable,
    )
}

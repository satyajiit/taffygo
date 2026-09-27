// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

/** Toggle and remove leave screen SCR-602 to the repository. */
internal fun reduceSkillDetail(
    state: SkillDetailUiState,
    intent: SkillDetailIntent,
): SkillDetailUiState = when (intent) {
    is SkillDetailIntent.AcceptRecorded,
    SkillDetailIntent.LoadRecordedReview,
    SkillDetailIntent.Toggle,
    SkillDetailIntent.Remove,
    -> state
}

/** The projection from the skills port to screen SCR-602. */
internal fun projectSkillDetail(
    snapshot: SkillsRepository.Snapshot,
    skillId: String,
    removeResult: SkillsRepository.RemoveResult? = null,
): SkillDetailUiState {
    val found = snapshot.skills.firstOrNull { it.id == skillId }
    return SkillDetailUiState(
        availability = snapshot.availability,
        skill = found?.let {
            SkillDetailUiState.Skill(
                id = it.id,
                name = plainSkillName(it.id),
                enabled = it.enabled,
                builtIn = it.builtIn,
                readiness = it.readiness,
                mayUse = it.mayUse,
                origin = it.origin,
                version = it.version,
                stepCount = it.stepCount,
                lifecycle = it.lifecycle,
                needsRecordedReview = it.needsRecordedReview,
                recorded = it.recorded,
                review = it.review,
            )
        },
        removeResult = removeResult,
    )
}

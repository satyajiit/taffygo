// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import com.taffygo.browser.ui.core.model.SavedFlowReview

/** Screen SCR-602 — one installed ability. */
data class SkillDetailUiState(
    val availability: SkillsRepository.Availability = SkillsRepository.Availability.LOADING,
    val skill: Skill? = null,
    val removeResult: SkillsRepository.RemoveResult? = null,
    val acceptResult: SkillsRepository.MutationResult? = null,
    val reviewLoading: Boolean = false,
    val reviewFailed: Boolean = false,
) {
    /** The ability this screen is about. */
    data class Skill(
        val id: String,
        val name: String = id,
        val enabled: Boolean,
        val builtIn: Boolean,
        val readiness: SkillsRepository.Readiness = SkillsRepository.Readiness.READY,
        val mayUse: List<SkillsRepository.MayUse>,
        val origin: String? = null,
        val version: UInt = 0u,
        val stepCount: UInt = 0u,
        val lifecycle: SkillsRepository.Lifecycle = SkillsRepository.Lifecycle.ACTIVE,
        val needsRecordedReview: Boolean = false,
        val recorded: Boolean = false,
        val review: SavedFlowReview? = null,
    )
}

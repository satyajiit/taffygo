// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow

/** Process-local adapter used only by reducer tests. */
internal class EmptySkillsRepository : SkillsRepository {
    private val internal = MutableStateFlow(
        SkillsRepository.Snapshot(
            availability = SkillsRepository.Availability.READY,
            skills = SkillsRepository.previewBuiltIns(),
        ),
    )
    override val snapshot: StateFlow<SkillsRepository.Snapshot> = internal

    override suspend fun setEnabled(id: String, enabled: Boolean) {
        if (internal.value.skills.none { it.id == id }) return
        internal.value = internal.value.copy(
            skills = internal.value.skills.map { skill ->
                if (skill.id == id) skill.copy(enabled = enabled) else skill
            },
        )
    }

    override suspend fun remove(id: String): SkillsRepository.RemoveResult {
        val skill = internal.value.skills.firstOrNull { it.id == id }
            ?: return SkillsRepository.RemoveResult.NOT_FOUND
        return if (skill.builtIn) {
            SkillsRepository.RemoveResult.CANNOT_REMOVE
        } else {
            internal.value = internal.value.copy(
                skills = internal.value.skills.filterNot { it.id == id },
            )
            SkillsRepository.RemoveResult.REMOVED
        }
    }

    override suspend fun teachSite(
        name: String,
        recording: SkillsRepository.ObservedSiteRecording,
    ): SkillsRepository.MutationResult = SkillsRepository.MutationResult.INVALID

    override suspend fun updateSite(
        id: String,
        expectedVersion: UInt,
        recording: SkillsRepository.ObservedSiteRecording,
    ): SkillsRepository.MutationResult = SkillsRepository.MutationResult.INVALID
}

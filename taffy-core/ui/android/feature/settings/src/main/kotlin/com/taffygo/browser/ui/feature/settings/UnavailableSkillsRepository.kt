// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow

/** The skills list is missing. Toggles and removes do not pretend otherwise. */
class UnavailableSkillsRepository : SkillsRepository {

    override val snapshot: StateFlow<SkillsRepository.Snapshot> =
        MutableStateFlow(
            SkillsRepository.Snapshot(availability = SkillsRepository.Availability.UNAVAILABLE),
        ).asStateFlow()

    override suspend fun setEnabled(id: String, enabled: Boolean) = Unit

    override suspend fun remove(id: String): SkillsRepository.RemoveResult =
        SkillsRepository.RemoveResult.UNAVAILABLE

    override suspend fun teachSite(
        name: String,
        recording: SkillsRepository.ObservedSiteRecording,
    ): SkillsRepository.MutationResult = SkillsRepository.MutationResult.UNAVAILABLE

    override suspend fun updateSite(
        id: String,
        expectedVersion: UInt,
        recording: SkillsRepository.ObservedSiteRecording,
    ): SkillsRepository.MutationResult = SkillsRepository.MutationResult.UNAVAILABLE
}

// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow

/**
 * Personality cannot be saved. Choosing a preset does not claim that it was.
 */
class UnavailablePersonalityRepository : PersonalityRepository {

    override val snapshot: StateFlow<PersonalityRepository.Snapshot> =
        MutableStateFlow(
            PersonalityRepository.Snapshot(
                availability = PersonalityRepository.Availability.UNAVAILABLE,
            ),
        ).asStateFlow()

    override suspend fun choosePreset(preset: PersonalityRepository.Preset) = Unit

    override suspend fun setScales(scales: PersonalityRepository.Scales) = Unit
}

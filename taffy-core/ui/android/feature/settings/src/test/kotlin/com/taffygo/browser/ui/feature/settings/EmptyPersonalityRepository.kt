// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow

/** Process-local adapter used only by reducer tests. */
internal class EmptyPersonalityRepository : PersonalityRepository {
    private val internal = MutableStateFlow(
        PersonalityRepository.Snapshot(
            availability = PersonalityRepository.Availability.READY,
            selected = PersonalityRepository.Preset.CAREFUL_RESEARCHER,
            scales = PersonalityRepository.Preset.CAREFUL_RESEARCHER.scales(),
        ),
    )
    override val snapshot: StateFlow<PersonalityRepository.Snapshot> = internal

    override suspend fun choosePreset(preset: PersonalityRepository.Preset) {
        internal.value = internal.value.copy(selected = preset, scales = preset.scales())
    }

    override suspend fun setScales(scales: PersonalityRepository.Scales) {
        internal.value = internal.value.copy(scales = scales.clamped())
    }
}

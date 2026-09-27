// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

/** Screen SCR-603 — How Taffy talks. */
data class PersonalityUiState(
    val availability: PersonalityRepository.Availability =
        PersonalityRepository.Availability.LOADING,
    val selected: PersonalityRepository.Preset? = null,
    val presets: List<PersonalityRepository.Preset> = PersonalityRepository.Preset.entries,
    val choiceNotSaved: Boolean = false,
)

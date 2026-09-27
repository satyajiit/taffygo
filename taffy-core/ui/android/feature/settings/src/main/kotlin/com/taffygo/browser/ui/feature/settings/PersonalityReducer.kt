// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

/** Choosing and opening leave screen SCR-603 to the repository and navigator. */
internal fun reducePersonality(
    state: PersonalityUiState,
    intent: PersonalityIntent,
): PersonalityUiState = when (intent) {
    is PersonalityIntent.ChoosePreset,
    PersonalityIntent.OpenTuning,
    -> state
}

/** The projection from the Personality port to screen SCR-603. */
internal fun projectPersonality(
    snapshot: PersonalityRepository.Snapshot,
    choiceNotSaved: Boolean,
): PersonalityUiState = PersonalityUiState(
    availability = snapshot.availability,
    selected = snapshot.selected,
    presets = PersonalityRepository.Preset.entries,
    choiceNotSaved = choiceNotSaved,
)

// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

/** Scale changes leave screen SCR-604 to the repository. */
internal fun reducePersonalityTuning(
    state: PersonalityTuningUiState,
    intent: PersonalityTuningIntent,
): PersonalityTuningUiState = when (intent) {
    is PersonalityTuningIntent.SetScale -> state
}

/** The projection from the Personality port to screen SCR-604. */
internal fun projectPersonalityTuning(
    snapshot: PersonalityRepository.Snapshot,
    notSaved: Boolean,
): PersonalityTuningUiState = PersonalityTuningUiState(
    availability = snapshot.availability,
    scales = snapshot.scales,
    notSaved = notSaved,
)

internal fun applyTuningScale(
    scales: PersonalityRepository.Scales,
    axis: PersonalityTuningIntent.Axis,
    value: Int,
): PersonalityRepository.Scales {
    val next = when (axis) {
        PersonalityTuningIntent.Axis.PACE -> scales.copy(pace = value)
        PersonalityTuningIntent.Axis.LENGTH -> scales.copy(length = value)
        PersonalityTuningIntent.Axis.CHECK_IN -> scales.copy(checkIn = value)
    }
    return next.clamped()
}

// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import androidx.compose.ui.graphics.vector.ImageVector
import com.taffygo.browser.ui.core.ui.TaffyIcon

internal fun personalityPresetTitleRes(preset: PersonalityRepository.Preset): Int = when (preset) {
    PersonalityRepository.Preset.CAREFUL_RESEARCHER ->
        R.string.taffy_personality_preset_researcher
    PersonalityRepository.Preset.QUICK_SHOPPER -> R.string.taffy_personality_preset_shopper
    PersonalityRepository.Preset.TRIP_PLANNER -> R.string.taffy_personality_preset_trip
}

internal fun personalityPresetSummaryRes(preset: PersonalityRepository.Preset): Int =
    when (preset) {
        PersonalityRepository.Preset.CAREFUL_RESEARCHER ->
            R.string.taffy_personality_preset_researcher_summary
        PersonalityRepository.Preset.QUICK_SHOPPER ->
            R.string.taffy_personality_preset_shopper_summary
        PersonalityRepository.Preset.TRIP_PLANNER ->
            R.string.taffy_personality_preset_trip_summary
    }

internal fun personalityPresetIcon(preset: PersonalityRepository.Preset): ImageVector =
    when (preset) {
        PersonalityRepository.Preset.CAREFUL_RESEARCHER -> TaffyIcon.MagnifyingGlass
        PersonalityRepository.Preset.QUICK_SHOPPER -> TaffyIcon.Tag
        PersonalityRepository.Preset.TRIP_PLANNER -> TaffyIcon.GlobeSimple
    }

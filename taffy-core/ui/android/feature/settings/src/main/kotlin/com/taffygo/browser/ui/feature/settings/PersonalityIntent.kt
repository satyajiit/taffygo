// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

/** Everything screen SCR-603 can be asked to do. */
sealed interface PersonalityIntent {

    /** Choose a starting point for how Taffy talks. */
    data class ChoosePreset(val preset: PersonalityRepository.Preset) : PersonalityIntent

    /** Open the three scales. */
    data object OpenTuning : PersonalityIntent
}

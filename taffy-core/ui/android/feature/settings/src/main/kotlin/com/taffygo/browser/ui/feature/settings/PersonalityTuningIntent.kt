// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

/** Everything screen SCR-604 can be asked to do. */
sealed interface PersonalityTuningIntent {

    /** One of the three scales. */
    enum class Axis { PACE, LENGTH, CHECK_IN }

    /** Set one scale. [value] is 0, 1, or 2. */
    data class SetScale(val axis: Axis, val value: Int) : PersonalityTuningIntent
}

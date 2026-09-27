// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.ui

/**
 * The four parts of a day the start page has paintings for.
 *
 * The day is divided by what the light is doing rather than by the clock's own
 * quarters: morning ends when the light stops being morning light, not at noon
 * exactly; evening is the short warm part before it is properly dark; and night
 * carries the small hours, which is why [StartSceneMember.partFor] wraps into
 * it rather than giving it a range.
 */
enum class TaffyDayPart {
    MORNING,
    AFTERNOON,
    EVENING,
    NIGHT,
    ;

    /** The lowercase name the pack's member paths are built from. */
    val member: String
        get() = when (this) {
            MORNING -> "morning"
            AFTERNOON -> "afternoon"
            EVENING -> "evening"
            NIGHT -> "night"
        }
}

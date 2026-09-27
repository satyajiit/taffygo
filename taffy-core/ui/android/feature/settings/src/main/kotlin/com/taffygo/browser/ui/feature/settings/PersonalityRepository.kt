// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import kotlinx.coroutines.flow.StateFlow

/**
 * How Taffy talks. Never what Taffy may do.
 *
 * A preset is a starting point for three scales. Choosing one does not grant
 * a permission, widen a site, or add a skill.
 */
interface PersonalityRepository {

    /** What screens SCR-603 and SCR-604 render. */
    val snapshot: StateFlow<Snapshot>

    /** Pick a preset and the scales it starts from. */
    suspend fun choosePreset(preset: Preset)

    /** Replace the three scales. Values outside 0..2 are kept within that range. */
    suspend fun setScales(scales: Scales)

    /** One reading of Personality. */
    data class Snapshot(
        val availability: Availability,
        val selected: Preset? = null,
        val scales: Scales = Scales(),
    )

    /** Whether Personality can be saved on this phone. */
    enum class Availability { LOADING, READY, UNAVAILABLE }

    /** A named starting point. */
    enum class Preset {
        CAREFUL_RESEARCHER,
        QUICK_SHOPPER,
        TRIP_PLANNER,
        ;

        /** The scales this preset starts from. */
        fun scales(): Scales = when (this) {
            CAREFUL_RESEARCHER -> Scales(pace = 0, length = 1, checkIn = 0)
            QUICK_SHOPPER -> Scales(pace = 2, length = 0, checkIn = 2)
            TRIP_PLANNER -> Scales(pace = 1, length = 1, checkIn = 1)
        }
    }

    /**
     * Careful↔Quick, Concise↔Chatty, Asks first↔Acts on approved plans.
     * Each axis is 0, 1, or 2.
     */
    data class Scales(
        val pace: Int = 0,
        val length: Int = 1,
        val checkIn: Int = 0,
    ) {
        /** The same scales with each axis kept in 0..2. */
        fun clamped(): Scales = Scales(
            pace = pace.coerceIn(AXIS_MIN, AXIS_MAX),
            length = length.coerceIn(AXIS_MIN, AXIS_MAX),
            checkIn = checkIn.coerceIn(AXIS_MIN, AXIS_MAX),
        )
    }

    companion object {
        const val AXIS_MIN: Int = 0
        const val AXIS_MAX: Int = 2
        const val AXIS_MID: Int = 1
    }
}

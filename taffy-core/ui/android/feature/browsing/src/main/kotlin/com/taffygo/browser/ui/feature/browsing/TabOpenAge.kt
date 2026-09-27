// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

/**
 * How long a tab has been open, as a bucket the card can name.
 *
 * Zero [openedAtEpochMillis] means the engine did not say, so there is no
 * duration to draw. A clock that runs backwards is treated the same way.
 */
internal data class TabOpenAge(
    val minutes: Long,
) {
    val justOpened: Boolean get() = minutes < 1
    val underAnHour: Boolean get() = minutes in 1 until 60
    val underADay: Boolean get() = minutes in 60 until MINUTES_PER_DAY

    val hours: Long get() = minutes / 60
    val days: Long get() = minutes / MINUTES_PER_DAY

    companion object {
        fun of(openedAtEpochMillis: Long, nowEpochMillis: Long): TabOpenAge? {
            if (openedAtEpochMillis <= 0L || nowEpochMillis < openedAtEpochMillis) return null
            return TabOpenAge((nowEpochMillis - openedAtEpochMillis) / MILLIS_PER_MINUTE)
        }

        /**
         * Whether a card may wear a duration at all.
         *
         * A tab that has been nowhere has no page-time to name — the clock
         * started when the empty tab was created, which is not a visit.
         */
        fun mayShowOn(card: TabCard): Boolean = !card.hasBeenNowhere

        private const val MILLIS_PER_MINUTE = 60_000L
        private const val MINUTES_PER_DAY = 1_440L
    }
}

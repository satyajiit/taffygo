// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import java.security.MessageDigest

/**
 * One page opened on this phone, as History lists it.
 *
 * Private visits and Taffy's working trail never belong here. The repository
 * is the boundary that drops them; the projection drops them again so a
 * leaky store cannot invent a person's trail from Taffy's.
 */
data class HistoryVisit(
    val id: Id,
    val title: String,
    val host: String,
    val visitedAtEpochMillis: Long,
    val isPrivate: Boolean = false,
    val isTaffyWorkingTrail: Boolean = false,
    /** Exact HTTP(S) address retained for open and local address-bar suggestions. */
    val address: String = host,
) {
    /** Opaque visit identifier. */
    @JvmInline
    value class Id(val value: String) {
        companion object {
            /**
             * Stable opaque identity for a stored visit.
             *
             * The address and visit time name the row Chromium will mutate.
             * Hashing keeps the address out of Compose keys, test tags, and
             * accessibility diagnostics while remaining stable across query
             * refreshes and list reordering.
             */
            fun fromStoredVisit(exactAddress: String, visitedAtEpochMillis: Long): Id {
                val input = "$visitedAtEpochMillis\u0000$exactAddress".encodeToByteArray()
                // A history projection may mint 1,024 rows at once. Provider
                // lookup for every row dominated the tiny digest itself, so
                // keep one reset-after-digest instance per calling thread.
                val digest =
                    HASHER.get()?.digest(input)
                        ?: MessageDigest.getInstance("SHA-256").digest(input)
                return Id(
                    buildString(STORED_ID_LENGTH) {
                        append(STORED_ID_PREFIX)
                        digest.forEach { byte ->
                            val unsigned = byte.toInt() and 0xff
                            append(HEX[unsigned ushr 4])
                            append(HEX[unsigned and 0x0f])
                        }
                    },
                )
            }

            private const val STORED_ID_PREFIX = "history-"
            private const val STORED_ID_LENGTH = 72
            private const val HEX = "0123456789abcdef"
            private val HASHER = ThreadLocal.withInitial {
                MessageDigest.getInstance("SHA-256")
            }
        }
    }
}

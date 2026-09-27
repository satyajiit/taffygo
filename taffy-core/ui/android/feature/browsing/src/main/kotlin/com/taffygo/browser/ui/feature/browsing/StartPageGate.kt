// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import com.taffygo.browser.ui.core.model.TaffyPartAvailability
import com.taffygo.browser.ui.core.model.TaffyPartHold
import com.taffygo.browser.ui.core.model.TaffyPartId
import com.taffygo.browser.ui.core.model.TaffyPartProgress
import com.taffygo.browser.ui.core.model.TaffyPartPurpose
import com.taffygo.browser.ui.core.model.TaffyPartsState

/**
 * Whether the start page may be shown, and how far the install it waits on is.
 *
 * Page intelligence cannot start without the Python library, so an empty tab
 * and screen SCR-102 draw the start page only once that library is installed.
 * Closed until then: a snapshot that has not arrived, a build with no library
 * row, and a download that is still moving all answer "not ready". Extra
 * packages — country flags, optional toolkits — do not hold the start page.
 */
data class StartPageGate(
    /** Whether the start page may be drawn. */
    val ready: Boolean = false,
    /**
     * Whether the browser has described delivery to this surface at all.
     *
     * False is the state this screen used to be unable to name, and it is
     * the one that stalled: with no snapshot there is no library row, so the
     * screen drew an indeterminate bar with no percentage and no way out and
     * kept drawing it, whatever had gone wrong underneath. It is a separate
     * field rather than "no part id" because the two have different answers
     * — one waits on the core, the other waits on a download.
     */
    val answered: Boolean = false,
    /** The library's identity, when the catalog named one. */
    val partId: TaffyPartId? = null,
    /** Bytes on the device right now. */
    val downloadedBytes: Long = 0,
    /** Total size in bytes, or zero when it is not known. */
    val totalBytes: Long = 0,
    /** Why the last attempt did not finish, when one did not. */
    val hold: TaffyPartHold? = null,
    /** How much of the library is on the device, when the catalog named one. */
    val availability: TaffyPartAvailability? = null,
) {
    /**
     * Progress from 0 to 1, or null when the total is not known — which the
     * screen shows as unknown rather than as a guess.
     */
    val fraction: Float?
        get() = totalBytes.takeIf { it > 0 }
            ?.let { (downloadedBytes.toFloat() / it).coerceIn(0f, 1f) }

    /**
     * Whether asking again could finish.
     *
     * A silent browser is always retryable: there is no row to name, so the
     * only thing left to ask is the core itself, and that is exactly the
     * case where a person is looking at a bar that never moves. Everything
     * else is the library row's own answer.
     */
    val canRetry: Boolean
        get() = when {
            !answered -> true
            partId == null -> false
            else -> hold?.canRetry ?: (
                availability == TaffyPartAvailability.MISSING ||
                    availability == TaffyPartAvailability.CHECKING
                )
        }
}

/**
 * The start-page answer from what the delivery plane currently reports.
 *
 * Every Python-library row must be installed. Country flags and extra
 * packages are not this gate. A missing row is not ready rather than ready
 * by omission: the start page cannot be shown on the strength of a catalog
 * the surface has not seen.
 *
 * "Has not seen" is now the literal test rather than a stand-in for it. Two
 * cases used to collapse into the same closed gate and they are not the same
 * thing:
 *
 * - The browser has said nothing ([TaffyPartsState.answered] false). Still
 *   closed — a catalog nobody has read cannot open it — but the screen can
 *   now say so and offer the core a retry, instead of showing a bar that
 *   never moves for the life of the process.
 * - The browser has spoken and this build publishes nothing for this device
 *   ([TaffyPartsState.supported] false). Open. Page intelligence is not
 *   coming here at all, so there is no install to wait for, and holding the
 *   browser shut against a row that will never exist is the stall rather
 *   than the safeguard.
 */
internal fun startPageGate(
    parts: TaffyPartsState,
    liveProgress: Map<TaffyPartId, TaffyPartProgress> = emptyMap(),
): StartPageGate {
    if (!parts.answered) {
        return StartPageGate()
    }
    val libraries = parts.parts.filter { it.purpose == TaffyPartPurpose.PYTHON_LIBRARY }
    val part = libraries.firstOrNull()
        ?: return StartPageGate(ready = !parts.supported, answered = true)
    val live = liveProgress[part.id]
    val downloaded = when {
        live != null &&
            live.version == part.version &&
            live.downloadedBytes > part.downloadedBytes -> live.downloadedBytes
        else -> part.downloadedBytes
    }
    val total = when {
        live != null && live.version == part.version && live.totalBytes > 0 -> live.totalBytes
        else -> part.totalBytes
    }
    return StartPageGate(
        ready = libraries.all { it.availability == TaffyPartAvailability.READY },
        answered = true,
        partId = part.id,
        downloadedBytes = downloaded,
        totalBytes = total,
        hold = part.hold,
        availability = part.availability,
    )
}

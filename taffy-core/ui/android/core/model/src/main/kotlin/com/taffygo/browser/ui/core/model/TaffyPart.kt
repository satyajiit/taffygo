// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.model

/**
 * One part of Taffy the device downloads for itself (screen SCR-203).
 *
 * Not one of your downloads. These are the pieces TaffyGo fetches so that it
 * can work — a Python library, a model, a block list — kept apart from the
 * files you download from the web because you did not ask for them and cannot
 * open them. Sizes are bytes; the screen formats them.
 */
data class TaffyPart(
    /** Identity of the part. */
    val id: TaffyPartId,
    /** Which version of it this is. */
    val version: String,
    /** What it is for. */
    val purpose: TaffyPartPurpose,
    /** How much of it is here. */
    val availability: TaffyPartAvailability,
    /** Bytes on the device. */
    val downloadedBytes: Long,
    /** Total size in bytes, or zero when nothing is published to download. */
    val totalBytes: Long,
    /** How many downloads have been attempted and ended. */
    val attempts: Int,
    /** Why the last attempt did not finish, when one did not. */
    val hold: TaffyPartHold?,
) {
    /**
     * Progress from 0 to 1, or null when there is no published size — which
     * the screen shows as unknown rather than as a guess, the same rule your
     * own downloads follow.
     */
    val fraction: Float?
        get() = totalBytes.takeIf { it > 0 }
            ?.let { (downloadedBytes.toFloat() / it).coerceIn(0f, 1f) }

    /** Whether asking for this part again could finish. */
    val canRetry: Boolean
        get() = hold?.canRetry == true
}

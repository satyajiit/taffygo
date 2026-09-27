// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.model

/**
 * How far one running download of a part has got, right now.
 *
 * Separate from [TaffyPart] because it arrives by a different route and for a
 * different reason: the byte count moves several times a second and nothing
 * decides anything with it, so the browser pushes it straight to the screen
 * rather than putting it through the isolated core. What [TaffyPart] carries
 * is where a download actually got to when it stopped.
 */
data class TaffyPartProgress(
    /** Which part is moving. */
    val id: TaffyPartId,
    /** Which version is being downloaded. */
    val version: String,
    /** Bytes on the device right now. */
    val downloadedBytes: Long,
    /** Total size in bytes, or zero when it is not known. */
    val totalBytes: Long,
) {
    /** Progress from 0 to 1, or null when the total is not known. */
    val fraction: Float?
        get() = totalBytes.takeIf { it > 0 }
            ?.let { (downloadedBytes.toFloat() / it).coerceIn(0f, 1f) }
}

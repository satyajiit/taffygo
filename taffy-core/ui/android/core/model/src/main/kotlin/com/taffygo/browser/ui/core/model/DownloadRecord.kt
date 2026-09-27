// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.model

/**
 * One download (screen SCR-203). Sizes are bytes; the screen formats them.
 */
data class DownloadRecord(
    /** Identity of the download. */
    val id: DownloadId,
    /** The file name as it will appear on the device. */
    val fileName: String,
    /** The host the file came from. */
    val host: String,
    /** Total size in bytes, or null while the server has not said. */
    val totalBytes: Long?,
    /** Bytes written so far. */
    val downloadedBytes: Long,
    /** Where the download has got to. */
    val state: DownloadState,
    /** Actions the profile-owned download provider permits for this exact snapshot. */
    val allowedActions: Set<DownloadAction>,
    /** The browser's declared content type, never inferred from a file name. */
    val mimeType: String? = null,
) {
    /**
     * Progress from 0 to 1, or null when the total is unknown — which the
     * screen shows as an indeterminate state rather than as a guess.
     */
    val fraction: Float?
        get() = totalBytes?.takeIf { it > 0 }?.let { (downloadedBytes.toFloat() / it).coerceIn(0f, 1f) }
}

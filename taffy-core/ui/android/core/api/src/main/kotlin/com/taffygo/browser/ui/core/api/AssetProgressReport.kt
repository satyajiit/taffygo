// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.api

/**
 * One observation of a running download of one of Taffy's own parts.
 *
 * Generated-contract vocabulary, unprojected: the browser sends these four
 * values and this carries them. Turning them into something a screen renders
 * is a repository's job, the same as it is for every other Core API type.
 */
data class AssetProgressReport(
    /** Which part is moving. */
    val assetId: String,
    /** Which revision is being downloaded. */
    val assetRevision: String,
    /** Bytes on the device right now, the resumed part included. */
    val writtenBytes: ULong,
    /** Bytes the catalog says there are. */
    val totalBytes: ULong,
)

// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.model

/** Where a download has got to (screen SCR-203). */
enum class DownloadState(
    /** A short, compiled-in name, safe to record in an audit event. */
    val label: String,
) {
    /** Bytes are arriving. */
    RUNNING("running"),

    /** Held by the user or by the system. */
    PAUSED("paused"),

    /** The file is on the device. */
    COMPLETE("complete"),

    /** It did not finish, and the reason is shown. */
    FAILED("failed"),
}

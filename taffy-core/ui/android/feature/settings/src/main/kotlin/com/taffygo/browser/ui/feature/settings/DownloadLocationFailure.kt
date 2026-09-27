// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

/** The boundary that prevented a download-location choice from completing. */
enum class DownloadLocationFailure {
    SELECTION_UNAVAILABLE,
    WRITE_FAILED,
    READBACK_FAILED,
}

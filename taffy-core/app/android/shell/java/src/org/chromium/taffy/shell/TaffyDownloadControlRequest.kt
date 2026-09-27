// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.shell

import androidx.annotation.VisibleForTesting
import com.taffygo.browser.ui.core.model.DownloadAction
import com.taffygo.browser.ui.core.model.DownloadId

/** Content-free authority carried by one explicit download notification control. */
@VisibleForTesting(otherwise = VisibleForTesting.PACKAGE_PRIVATE)
data class TaffyDownloadControlRequest(
    val profileToken: String,
    val downloadId: DownloadId,
    val action: DownloadAction,
)

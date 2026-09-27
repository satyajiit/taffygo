// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.assistant

import com.taffygo.browser.ui.core.model.DownloadId
import com.taffygo.browser.ui.core.model.DownloadRecord

/** Actual files the browser currently admits for this task and browser session. */
data class TaskDownloadsUiState(
    val taskId: String? = null,
    val files: List<DownloadRecord> = emptyList(),
    val opening: DownloadId? = null,
    val failed: DownloadId? = null,
)

// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.app

/**
 * Not screen state: the receiver adopts or closes [copy] after the export has drained.
 * Only VERIFIED or INCOMPLETE may carry a copy: UNAVAILABLE never opened its destination.
 */
data class BackupExportResult(
    val status: BackupDocumentTransfer.WriteResult,
    val copy: BackupDocumentCopy? = null,
)

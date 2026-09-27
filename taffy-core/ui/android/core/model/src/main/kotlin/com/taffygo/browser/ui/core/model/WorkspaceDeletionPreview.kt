// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.model

/** Content-free records and token bound to one exact saved-workspace revision. */
data class WorkspaceDeletionPreview(
    val sources: UInt,
    val facts: UInt,
    val artifactMetadata: UInt,
    val derivedIndexes: UInt,
    val confirmationToken: String,
)

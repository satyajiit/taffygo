// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.task

/** UI-safe action proposal; it carries no policy grant or capability. */
data class PendingActionProjection(
    val id: String,
    val host: String?,
    val itemCount: Int,
    val summaryMessageKey: String,
)

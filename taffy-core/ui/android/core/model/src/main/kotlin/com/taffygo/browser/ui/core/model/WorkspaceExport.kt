// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.model

/** One bounded export rendered by the canonical Rust workspace runtime. */
data class WorkspaceExport(
    /** Correlation identifier for this request, never an authority reference. */
    val requestId: String,
    /** Workspace whose immutable revision was rendered. */
    val workspaceId: WorkspaceId,
    /** Exact revision used by the renderer. */
    val revision: ULong,
    /** Closed output format selected by the user. */
    val format: ExportFormat,
    /** Deterministic Markdown or CSV bytes represented as UTF-8 text. */
    val content: String,
)

// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.model

/**
 * What the export sheet offers at milestone M3 (UX spec section 7). Both are
 * deterministic and carry their sources as links. Spreadsheet and portable
 * document formats belong to milestone M7 and are deliberately absent: a format
 * the UI layer cannot produce byte-identically is not offered.
 */
enum class ExportFormat(
    /** A short, compiled-in name, safe to record in an audit event. */
    val label: String,
    /** The extension the saved file carries. */
    val extension: String,
) {
    /** Headings, a table, and a source list. */
    MARKDOWN("markdown", "md"),

    /** One row per output row, sources in their own column. */
    COMMA_SEPARATED("comma_separated", "csv"),
}

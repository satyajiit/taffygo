// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.model

/**
 * One page a task read (UX spec section 6, "Sources"). Excluding a source
 * removes its facts and marks the cells that depended on it as needing a new
 * source; the record itself is kept so the timeline stays honest.
 */
data class SourceRecord(
    /** Identity of this source within its workspace. */
    val id: SourceId,
    /** The page title as the page gave it. */
    val title: String,
    /** The host, shown instead of a full URL. */
    val host: String,
    /** When the page was read, in epoch milliseconds. */
    val readAtEpochMillis: Long,
    /** How many facts came from this page. */
    val factCount: Int,
    /** Whether the user removed it from scope. */
    val excluded: Boolean = false,
)

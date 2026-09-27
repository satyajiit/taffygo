// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.model

/**
 * A store of the person's own that a request carries whole (decision 0133).
 *
 * Not a list of pages: attaching History is not naming a thousand visits as
 * sources. It hands Taffy tools — a search over the store, a listing of it —
 * that the browser answers with bounded rows, and only while the task runs.
 * Library is absent on purpose: its records already reach Taffy as sources
 * through the Library tools, so it stays somewhere to go.
 */
enum class TaskAttachedStore(
    /** A short, compiled-in name, safe to record in an audit event. */
    val label: String,
) {
    /** Visits, searched by words or listed latest first. */
    HISTORY("history"),

    /** Bookmarks, searched by words or the tree flattened. */
    BOOKMARKS("bookmarks"),

    /** The person's own open tabs, listed. */
    OPEN_TABS("open_tabs"),
}

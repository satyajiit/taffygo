// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.model

/**
 * The template chips of UX spec section 7, plus the errand of decision 0087.
 *
 * The chip set is `[Open (OD-008)]`; the first three are the ones the
 * specification names, and the UI layer offers exactly them rather than
 * guessing at a fourth.
 *
 * [WEB_ERRAND] is not a chip. It is the shape a goal typed into the address bar
 * becomes when the person asked for something to be done rather than for
 * something to be read, so it is reached through the start page's box rather
 * than picked from a list.
 */
enum class TaskTemplate(
    /** A short, compiled-in name, safe to record in an audit event. */
    val label: String,
    /**
     * Whether this template is offered as a chip the person can pick.
     *
     * A flag on the row rather than a list somewhere else, so that a template
     * added later has to answer the question. A surface that enumerated every
     * member would otherwise offer a new one the moment it was declared, which
     * is how an errand would have turned up in the picker as a fourth chip
     * nobody designed.
     */
    val offeredAsChip: Boolean,
) {
    /** A comparison table across several pages. */
    COMPARE_PRODUCTS("compare_products", offeredAsChip = true),

    /** A brief that cites what it read. */
    SUMMARIZE_EVIDENCE("summarize_evidence", offeredAsChip = true),

    /** A table of sources and what each one supports. */
    BUILD_A_SOURCE_TABLE("build_a_source_table", offeredAsChip = true),

    /** An errand carried out on a site, which Taffy may have to find first. */
    WEB_ERRAND("web_errand", offeredAsChip = false),
    ;

    companion object {
        /** The templates a person may pick from, in product order. */
        val chips: List<TaskTemplate> = entries.filter { it.offeredAsChip }
    }
}

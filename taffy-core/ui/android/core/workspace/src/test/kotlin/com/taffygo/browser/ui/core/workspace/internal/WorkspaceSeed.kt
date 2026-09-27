// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.workspace.internal

import com.taffygo.browser.ui.core.model.Fact
import com.taffygo.browser.ui.core.model.FactId
import com.taffygo.browser.ui.core.model.FactKind
import com.taffygo.browser.ui.core.model.SourceId
import com.taffygo.browser.ui.core.model.SourceRecord
import com.taffygo.browser.ui.core.model.TaskDisplayState
import com.taffygo.browser.ui.core.model.TaskTemplate
import com.taffygo.browser.ui.core.model.Workspace
import com.taffygo.browser.ui.core.model.WorkspaceId

/**
 * The workspaces the UI host starts with.
 *
 * Three of them, deliberately: one that finished, one that finished with gaps,
 * and one the user stopped. A screen that has only ever been seen with a
 * successful workspace is a screen that rounds partial work up to success the
 * first time it meets one.
 */
internal object WorkspaceSeed {

    /** A fixed capture time, so nothing here changes between runs. */
    private const val CAPTURED_AT = 1_767_225_600_000L

    val workspaces: List<Workspace> = listOf(complete(), partlyDone(), stopped())

    private fun complete() = Workspace(
        id = WorkspaceId("ws_complete"),
        goal = "compare the two retention policies",
        state = TaskDisplayState.DONE,
        lastUpdatedEpochMillis = CAPTURED_AT,
        template = TaskTemplate.COMPARE_PRODUCTS,
        sources = listOf(
            source("docs.example.test", "Retention policy", factCount = 2),
            source("reviews.example.test", "Independent review", factCount = 1),
        ),
        facts = listOf(
            fact("wsc_1", "retention", "24 months", FactKind.FROM_THE_PAGE, "docs.example.test"),
            fact("wsc_2", "deletion", "on request", FactKind.SUMMARIZED, "docs.example.test"),
            fact("wsc_3", "audited", "yes", FactKind.TAFFY_INFERENCE, "reviews.example.test"),
        ),
    )

    private fun partlyDone() = Workspace(
        id = WorkspaceId("ws_partly"),
        goal = "compare prices across three listings",
        state = TaskDisplayState.PARTLY_DONE,
        lastUpdatedEpochMillis = CAPTURED_AT - 3_600_000L,
        template = TaskTemplate.COMPARE_PRODUCTS,
        sources = listOf(
            source("shop.example.test", "Product listing", factCount = 2),
            source("reviews.example.test", "Independent review", factCount = 1),
            source("gone.example.test", "gone.example.test", factCount = 0),
        ),
        facts = listOf(
            fact("wsp_1", "price", "62,999", FactKind.FROM_THE_PAGE, "shop.example.test", conflict = true),
            fact("wsp_2", "warranty", "2 years", FactKind.SUMMARIZED, "shop.example.test"),
            fact("wsp_3", "price", "61,499", FactKind.FROM_THE_PAGE, "reviews.example.test", conflict = true),
        ),
    )

    private fun stopped() = Workspace(
        id = WorkspaceId("ws_stopped"),
        goal = "collect the sources for the review",
        state = TaskDisplayState.STOPPED,
        lastUpdatedEpochMillis = CAPTURED_AT - 86_400_000L,
        template = TaskTemplate.BUILD_A_SOURCE_TABLE,
        sources = listOf(source("docs.example.test", "Retention policy", factCount = 1)),
        facts = listOf(
            fact("wss_1", "published", "March", FactKind.FROM_THE_PAGE, "docs.example.test"),
        ),
    )

    private fun source(host: String, title: String, factCount: Int) = SourceRecord(
        id = SourceId(host),
        title = title,
        host = host,
        readAtEpochMillis = CAPTURED_AT,
        factCount = factCount,
    )

    private fun fact(
        id: String,
        field: String,
        value: String,
        kind: FactKind,
        host: String,
        conflict: Boolean = false,
    ) = Fact(
        id = FactId(id),
        field = field,
        value = value,
        kind = kind,
        sources = listOf(SourceId(host)),
        hasConflict = conflict,
    )
}

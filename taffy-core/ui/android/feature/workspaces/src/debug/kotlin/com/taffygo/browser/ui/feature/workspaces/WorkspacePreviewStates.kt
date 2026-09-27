// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.workspaces

import com.taffygo.browser.ui.core.model.Fact
import com.taffygo.browser.ui.core.model.FactId
import com.taffygo.browser.ui.core.model.FactKind
import com.taffygo.browser.ui.core.model.SourceId
import com.taffygo.browser.ui.core.model.SourceRecord
import com.taffygo.browser.ui.core.model.TaskDisplayState
import com.taffygo.browser.ui.core.model.TaskTemplate
import com.taffygo.browser.ui.core.model.Workspace
import com.taffygo.browser.ui.core.model.WorkspaceId

/** Fixed states for this feature's previews. */
object WorkspacePreviewStates {

    private const val CAPTURED_AT = 1_767_225_600_000L
    private const val DOCS = "docs.example.test"
    private const val SHOP = "shop.example.test"

    private val workspace = Workspace(
        id = WorkspaceId("ws_partly"),
        goal = "compare prices across two listings",
        state = TaskDisplayState.PARTLY_DONE,
        lastUpdatedEpochMillis = CAPTURED_AT,
        template = TaskTemplate.COMPARE_PRODUCTS,
        sources = listOf(
            SourceRecord(SourceId(DOCS), "Retention policy", DOCS, CAPTURED_AT, 2),
            SourceRecord(SourceId(SHOP), "Product listing", SHOP, CAPTURED_AT, 1, excluded = true),
        ),
        facts = listOf(
            Fact(
                FactId("f1"),
                "price",
                "62,999",
                FactKind.FROM_THE_PAGE,
                listOf(SourceId(DOCS)),
                hasConflict = true,
            ),
            Fact(
                FactId("f2"),
                "warranty",
                "2 years",
                FactKind.SUMMARIZED,
                listOf(SourceId(SHOP)),
                needsANewSource = true,
            ),
        ),
    )

    /** Screen SCR-304 with three workspaces in three different states. */
    val list = projectWorkspaceList(
        workspaces = listOf(
            workspace,
            workspace.copy(
                id = WorkspaceId("ws_done"),
                goal = "compare the two retention policies",
                state = TaskDisplayState.DONE,
                lastUpdatedEpochMillis = CAPTURED_AT - 1_000,
            ),
            workspace.copy(
                id = WorkspaceId("ws_stopped"),
                goal = "collect the sources for the review",
                state = TaskDisplayState.STOPPED,
                lastUpdatedEpochMillis = CAPTURED_AT - 2_000,
            ),
        ),
        query = "",
    )

    /** Screen SCR-305 with a conflict and a cell that lost its source. */
    val detail = projectWorkspaceDetail(workspace)

    /** Screen SCR-306 for one source. */
    val sourceViewer = projectSourceViewer(workspace, SourceId(DOCS))

    /** Screen SCR-307 for one fact. */
    val factCorrection = projectFactCorrection(workspace, FactId("f1"))

    /** Screen SCR-309 with a rendered preview. */
    val exportSheet = ExportSheetUiState(
        preview = "# compare prices across two listings\n\nState: partly_done\n",
    )
}

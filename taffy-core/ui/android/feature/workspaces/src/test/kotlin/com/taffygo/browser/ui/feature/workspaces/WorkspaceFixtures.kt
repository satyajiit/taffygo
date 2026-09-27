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

/**
 * Fixtures the workspace tests share.
 *
 * They are built here rather than taken from the seed the data layer ships, so
 * a change to the seed cannot quietly change what these tests assert.
 */
internal fun source(
    id: String,
    host: String,
    readAt: Long = 1_000,
    factCount: Int = 2,
    excluded: Boolean = false,
) = SourceRecord(
    id = SourceId(id),
    title = "Page at $host",
    host = host,
    readAtEpochMillis = readAt,
    factCount = factCount,
    excluded = excluded,
)

internal fun fact(
    id: String,
    field: String,
    value: String,
    sources: List<String>,
    kind: FactKind = FactKind.FROM_THE_PAGE,
    correction: String? = null,
    hasConflict: Boolean = false,
    needsANewSource: Boolean = false,
) = Fact(
    id = FactId(id),
    field = field,
    value = value,
    kind = kind,
    sources = sources.map(::SourceId),
    correction = correction,
    hasConflict = hasConflict,
    needsANewSource = needsANewSource,
)

internal fun workspace(
    id: String = "ws_0",
    goal: String = "Compare retention policies",
    state: TaskDisplayState = TaskDisplayState.DONE,
    lastUpdated: Long = 2_000,
    sources: List<SourceRecord> = listOf(source("src_0", "docs.example.test")),
    facts: List<Fact> = emptyList(),
) = Workspace(
    id = WorkspaceId(id),
    goal = goal,
    state = state,
    lastUpdatedEpochMillis = lastUpdated,
    template = TaskTemplate.COMPARE_PRODUCTS,
    sources = sources,
    facts = facts,
)

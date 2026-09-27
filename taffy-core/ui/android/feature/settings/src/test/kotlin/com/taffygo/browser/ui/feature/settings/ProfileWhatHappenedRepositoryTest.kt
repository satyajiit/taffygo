// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import com.taffygo.browser.ui.core.browser.FilteringSettings
import com.taffygo.browser.ui.core.model.SourceId
import com.taffygo.browser.ui.core.model.SourceRecord
import com.taffygo.browser.ui.core.model.TaskDisplayState
import com.taffygo.browser.ui.core.model.TaskTemplate
import com.taffygo.browser.ui.core.model.Workspace
import com.taffygo.browser.ui.core.model.WorkspaceId
import java.time.ZoneId
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import taffy.core_api.CoreAvailability

/** The product log projects only bounded facts its two owning stores publish. */
class ProfileWhatHappenedRepositoryTest {
    private val utc = ZoneId.of("UTC")

    @Test
    fun `ready projects latest workspace outcomes and the exact blocking count`() {
        val snapshot = projectWhatHappenedSnapshot(
            availability = CoreAvailability.READY,
            workspaces = listOf(
                workspace("done", 2_000L, TaskDisplayState.DONE, "news.example"),
                workspace("failed", 3_000L, TaskDisplayState.FAILED, "shop.example"),
                workspace("stopped", 1_000L, TaskDisplayState.STOPPED, "news.example"),
            ),
            filtering = FilteringSettings(blockedThisWeek = 17L),
            zoneId = utc,
        )

        assertEquals(listOf("failed", "done", "stopped"), snapshot.events.map { it.workspaceId })
        assertEquals(
            listOf(
                WhatHappenedRepository.Kind.TASK_FAILED,
                WhatHappenedRepository.Kind.WORKSPACE_RESULT,
                WhatHappenedRepository.Kind.TASK_STOPPED,
            ),
            snapshot.events.map { it.kind },
        )
        assertEquals(17L, snapshot.blockedRequestsThisWeek)
    }

    @Test
    fun `unavailable never leaks stale workspace or filtering facts`() {
        val snapshot = projectWhatHappenedSnapshot(
            availability = CoreAvailability.UNAVAILABLE,
            workspaces = listOf(workspace("hidden", 1_000L, TaskDisplayState.DONE, "hidden.test")),
            filtering = FilteringSettings(blockedThisWeek = 99L),
            zoneId = utc,
        )

        assertEquals(YouSurfaceAvailability.UNAVAILABLE, snapshot.availability)
        assertTrue(snapshot.events.isEmpty())
        assertNull(snapshot.blockedRequestsThisWeek)
    }

    @Test
    fun `multiple sources project only their count`() {
        val sourceA = source("a", "one.example")
        val sourceB = source("b", "two.example")
        val workspace = workspace("mixed", 1_000L, TaskDisplayState.DONE, "unused")
            .copy(sources = listOf(sourceA, sourceB))

        val event = projectWhatHappenedSnapshot(
            availability = CoreAvailability.READY,
            workspaces = listOf(workspace),
            filtering = FilteringSettings(),
            zoneId = utc,
        ).events.single()

        assertEquals(2, event.sourceCount)
    }

    @Test
    fun `running paused and waiting workspaces are not presented as history`() {
        val snapshot = projectWhatHappenedSnapshot(
            availability = CoreAvailability.READY,
            workspaces = listOf(
                workspace("running", 4_000L, TaskDisplayState.RUNNING, "one.example"),
                workspace("paused", 3_000L, TaskDisplayState.PAUSED, "two.example"),
                workspace("waiting", 2_000L, TaskDisplayState.WAITING_FOR_YOU, "three.example"),
                workspace("done", 1_000L, TaskDisplayState.DONE, "four.example"),
            ),
            filtering = FilteringSettings(),
            zoneId = utc,
        )

        assertEquals(listOf("done"), snapshot.events.map { it.workspaceId })
    }

    private fun workspace(
        id: String,
        updated: Long,
        state: TaskDisplayState,
        host: String,
    ): Workspace = Workspace(
        id = WorkspaceId(id),
        goal = "",
        state = state,
        lastUpdatedEpochMillis = updated,
        template = TaskTemplate.SUMMARIZE_EVIDENCE,
        sources = listOf(source("source-$id", host)),
        facts = emptyList(),
    )

    private fun source(id: String, host: String): SourceRecord = SourceRecord(
        id = SourceId(id),
        title = "",
        host = host,
        readAtEpochMillis = 1_000L,
        factCount = 0,
    )
}

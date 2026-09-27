// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.assistant

import com.taffygo.browser.ui.core.model.Fact
import com.taffygo.browser.ui.core.model.FactId
import com.taffygo.browser.ui.core.model.FactKind
import com.taffygo.browser.ui.core.model.SourceId
import com.taffygo.browser.ui.core.model.SourceRecord
import com.taffygo.browser.ui.core.model.TaskTemplate
import com.taffygo.browser.ui.core.task.TaskArtifactProjection
import com.taffygo.browser.ui.core.task.TaskProjection
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import taffy.core_api.TaskArtifactKind
import taffy.core_api.TaskPhase

class BrowserRoleEligibilityTest {
    @Test
    fun `one kept artifact with live cited facts qualifies`() {
        assertTrue(sourcedTask().qualifiesForBrowserRoleOffer(ARTIFACT_ID))
    }

    @Test
    fun `a generated but unkept file is not accepted value`() {
        val task = sourcedTask().copy(
            artifacts = listOf(artifact(accepted = false)),
        )

        assertFalse(task.qualifiesForBrowserRoleOffer(ARTIFACT_ID))
    }

    @Test
    fun `an orphan citation refuses the role offer`() {
        val task = sourcedTask().copy(
            facts = listOf(fact(sources = listOf(SourceId("source-missing")))),
        )

        assertFalse(task.qualifiesForBrowserRoleOffer(ARTIFACT_ID))
    }

    @Test
    fun `output whose source was excluded refuses the role offer`() {
        val task = sourcedTask().copy(
            facts = listOf(fact(needsANewSource = true)),
        )

        assertFalse(task.qualifiesForBrowserRoleOffer(ARTIFACT_ID))
    }

    private fun sourcedTask(): TaskProjection {
        val source = SourceRecord(
            id = SOURCE_ID,
            title = "Evidence",
            host = "example.test",
            readAtEpochMillis = 10L,
            factCount = 1,
        )
        return TaskProjection(
            id = "task-1",
            revision = 3uL,
            phase = TaskPhase.COMPLETED,
            goal = "compare",
            template = TaskTemplate.COMPARE_PRODUCTS,
            progressBasisPoints = 10_000u,
            statusMessageKey = null,
            failure = null,
            pendingAction = null,
            sources = listOf(source),
            facts = listOf(fact()),
            workspaceId = "workspace-1",
            workspaceRevision = 4uL,
            artifacts = listOf(artifact(accepted = true)),
        )
    }

    private fun fact(
        sources: List<SourceId> = listOf(SOURCE_ID),
        needsANewSource: Boolean = false,
    ) = Fact(
        id = FactId("fact-1"),
        field = "price",
        value = "42",
        kind = FactKind.FROM_THE_PAGE,
        sources = sources,
        needsANewSource = needsANewSource,
    )

    private fun artifact(accepted: Boolean) = TaskArtifactProjection(
        id = ARTIFACT_ID,
        kind = TaskArtifactKind.MARKDOWN,
        workspaceRevision = 4uL,
        accepted = accepted,
    )

    private companion object {
        val SOURCE_ID = SourceId("source-1")
        const val ARTIFACT_ID = "artifact-1"
    }
}

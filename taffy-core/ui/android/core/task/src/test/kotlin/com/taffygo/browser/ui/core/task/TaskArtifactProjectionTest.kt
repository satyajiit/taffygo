// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.task

import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Test
import taffy.core_api.TaskArtifactKind

internal class TaskArtifactProjectionTest {
    @Test
    fun `every closed format maps to one safe filename and mime type`() {
        val expected = listOf(
            Triple(TaskArtifactKind.MARKDOWN, "taffy-task.md", "text/markdown"),
            Triple(TaskArtifactKind.CSV, "taffy-task.csv", "text/csv"),
            Triple(
                TaskArtifactKind.XLSX,
                "taffy-task.xlsx",
                "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet",
            ),
            Triple(TaskArtifactKind.PDF, "taffy-task.pdf", "application/pdf"),
            Triple(
                TaskArtifactKind.DOCX,
                "taffy-task.docx",
                "application/vnd.openxmlformats-officedocument.wordprocessingml.document",
            ),
            Triple(
                TaskArtifactKind.PPTX,
                "taffy-task.pptx",
                "application/vnd.openxmlformats-officedocument.presentationml.presentation",
            ),
            Triple(TaskArtifactKind.WAVE_AUDIO, "taffy-task.wav", "audio/wav"),
            Triple(
                TaskArtifactKind.FRAME_ARCHIVE,
                "taffy-task-frames.zip",
                "application/zip",
            ),
        )

        expected.forEach { (kind, name, mimeType) ->
            val artifact = TaskArtifactProjection("artifact-1", kind, 7u, accepted = true)
            assertEquals(name, artifact.suggestedFileName)
            assertEquals(mimeType, artifact.mimeType)
        }
    }

    @Test
    fun `transient payload never exposes its resident byte array`() {
        val original = byteArrayOf(1, 2, 3)
        val payload = TaskArtifactPayload(
            requestId = "request-1",
            taskId = "task-1",
            artifactId = "artifact-1",
            kind = TaskArtifactKind.PDF,
            content = original,
        )
        original[0] = 9
        val first = payload.copyContent()
        first[1] = 9

        assertArrayEquals(byteArrayOf(1, 2, 3), payload.copyContent())
    }
}

// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.task

import taffy.core_api.TaskArtifactKind

/** Content-free metadata for one reproducible task file. */
data class TaskArtifactProjection(
    val id: String,
    val kind: TaskArtifactKind,
    val workspaceRevision: ULong,
    val accepted: Boolean,
) {
    /** A fixed MIME type selected only from the closed contract enum. */
    val mimeType: String
        get() = when (kind) {
            TaskArtifactKind.MARKDOWN -> "text/markdown"
            TaskArtifactKind.CSV -> "text/csv"
            TaskArtifactKind.XLSX ->
                "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet"
            TaskArtifactKind.PDF -> "application/pdf"
            TaskArtifactKind.DOCX ->
                "application/vnd.openxmlformats-officedocument.wordprocessingml.document"
            TaskArtifactKind.PPTX ->
                "application/vnd.openxmlformats-officedocument.presentationml.presentation"
            TaskArtifactKind.WAVE_AUDIO -> "audio/wav"
            TaskArtifactKind.FRAME_ARCHIVE -> "application/zip"
        }

    /** Page text and model output never enter the Android picker filename. */
    val suggestedFileName: String
        get() = when (kind) {
            TaskArtifactKind.MARKDOWN -> "taffy-task.md"
            TaskArtifactKind.CSV -> "taffy-task.csv"
            TaskArtifactKind.XLSX -> "taffy-task.xlsx"
            TaskArtifactKind.PDF -> "taffy-task.pdf"
            TaskArtifactKind.DOCX -> "taffy-task.docx"
            TaskArtifactKind.PPTX -> "taffy-task.pptx"
            TaskArtifactKind.WAVE_AUDIO -> "taffy-task.wav"
            TaskArtifactKind.FRAME_ARCHIVE -> "taffy-task-frames.zip"
        }
}

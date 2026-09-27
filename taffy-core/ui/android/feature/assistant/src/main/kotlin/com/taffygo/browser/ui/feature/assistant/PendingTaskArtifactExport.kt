// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.assistant

import com.taffygo.browser.ui.core.task.TaskArtifactPayload
import com.taffygo.browser.ui.core.task.TaskArtifactProjection

/** Bytes held in UI memory only while one explicit platform handoff is active. */
data class PendingTaskArtifactExport(
    val artifact: TaskArtifactProjection,
    val payload: TaskArtifactPayload,
    val destination: TaskViewIntent.ArtifactDestination,
)

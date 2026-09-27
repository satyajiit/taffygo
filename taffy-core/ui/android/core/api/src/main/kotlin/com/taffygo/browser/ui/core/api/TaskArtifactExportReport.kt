// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.api

import taffy.core_api.TaskArtifactKind

/** One browser-custodied file produced for an exact explicit task request. */
data class TaskArtifactExportReport(
    val requestId: String,
    val taskId: String,
    val artifactId: String,
    val kind: TaskArtifactKind,
    /** Validated transient bytes; never part of a task snapshot or journal. */
    val content: ByteArray,
)

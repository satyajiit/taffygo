// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.task

import taffy.core_api.TaskArtifactKind

/** One transient browser-custodied file matched to an explicit UI request. */
class TaskArtifactPayload(
    val requestId: String,
    val taskId: String,
    val artifactId: String,
    val kind: TaskArtifactKind,
    content: ByteArray,
) {
    private val bytes = content.copyOf()

    /** Returns an isolated copy only when the trusted platform handoff needs it. */
    fun copyContent(): ByteArray = bytes.copyOf()
}

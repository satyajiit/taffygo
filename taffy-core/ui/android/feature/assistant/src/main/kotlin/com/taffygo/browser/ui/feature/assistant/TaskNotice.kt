// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.assistant

/**
 * Something about a task that the surfaces owe a person an explanation for,
 * and that the task itself cannot tell them.
 *
 * A notice is derived only from browser-owned Core API availability. It is
 * never inferred from elapsed time and never replaced by a Kotlin executor.
 */
enum class TaskNotice(
    /** A short, compiled-in name, safe to record in an audit event. */
    val label: String,
) {
    /** The profile service disconnected; browsing remains usable. */
    CORE_UNAVAILABLE("core_unavailable"),

    /** Repeated failures opened the profile circuit and require an explicit retry. */
    RETRY_REQUIRED("retry_required"),
}

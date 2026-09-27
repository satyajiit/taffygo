// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.task

import com.taffygo.browser.ui.core.common.TaffyResult
import com.taffygo.browser.ui.core.model.TaskInputRequest
import kotlinx.coroutines.flow.StateFlow

/**
 * The form Taffy is holding open, classified, and the two things a surface may
 * do about it.
 *
 * The Android face of `BrowserTaskInputEndpoint`, in the same relation to it
 * that [TaskRepository] is to the Core API facade: the endpoint describes, this
 * classifies, and every surface reads the classified answer. A description this
 * product cannot draw never becomes a request here — see the projection this
 * module owns — so no screen has to carry the "and what if the row means
 * nothing" branch.
 */
interface TaskInputRepository {
    /**
     * Whether a browser vault is behind this seam in this build.
     *
     * False is the closed answer: no surface offers to collect a value it has
     * nowhere to put.
     */
    val isAvailable: Boolean

    /** The classified form, or null when none is open or none could be drawn. */
    val request: StateFlow<TaskInputRequest?>

    /**
     * Hand what the person typed to the browser, keyed by field id.
     *
     * Passed straight through and retained nowhere on this layer.
     */
    suspend fun submit(requestId: String, values: Map<String, String>): TaffyResult<Unit>

    /** Tell the browser the person has finished with a widget on the page. */
    suspend fun completeInteractive(requestId: String): TaffyResult<Unit>
}

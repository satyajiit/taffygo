// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.task.internal

import com.taffygo.browser.ui.core.api.TaskInputClient
import com.taffygo.browser.ui.core.common.TaffyResult
import com.taffygo.browser.ui.core.common.di.TaffyProfileLifetime
import com.taffygo.browser.ui.core.model.TaskInputRequest
import com.taffygo.browser.ui.core.task.TaskInputRepository
import com.taffygo.browser.ui.core.task.taskInputRequestFrom
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.flow.stateIn

/** The only Android form repository: a closed projection over the browser seam. */
internal class CoreTaskInputRepository(
    private val client: TaskInputClient,
    lifetime: TaffyProfileLifetime,
) : TaskInputRepository {

    override val isAvailable: Boolean
        get() = client.isAvailable

    override val request: StateFlow<TaskInputRequest?> = client.described
        .map(::taskInputRequestFrom)
        .stateIn(
            scope = lifetime.scope,
            started = SharingStarted.Eagerly,
            initialValue = taskInputRequestFrom(client.described.value),
        )

    // Neither of these goes through `submitCoreApiCommand`, and that is the
    // point of the seam rather than an omission: the value never travels as a
    // Core API command, so it never reaches the admission ledger, the status
    // projection or the journal. What comes back is the browser's own verdict.
    override suspend fun submit(
        requestId: String,
        values: Map<String, String>,
    ): TaffyResult<Unit> = client.submitValues(requestId, values)

    override suspend fun completeInteractive(requestId: String): TaffyResult<Unit> =
        client.completeInteractive(requestId)
}

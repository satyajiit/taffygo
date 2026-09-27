// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.api

import com.taffygo.browser.ui.core.common.FailureReason
import com.taffygo.browser.ui.core.common.TaffyResult
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow

/**
 * The honest answer where nothing is behind the seam.
 *
 * A Gradle build has no browser process, so it has no vault to mint a value
 * reference in and no page to spend one against. This endpoint says exactly
 * that: it never holds a form open, so no surface offers to collect anything,
 * and if one asked anyway the answer is a closed refusal rather than a value
 * accepted into a process that cannot do anything with it.
 *
 * It is a real implementation rather than a null binding so that a build is
 * truthful about what it has: the graph resolves, the screens compose, and
 * every one of them draws the state that says the browser is not here — which
 * is a thing a person can be shown, unlike a missing binding.
 */
class UnavailableTaskInputEndpoint : BrowserTaskInputEndpoint {
    override val isAvailable: Boolean = false

    override val described: StateFlow<TaskInputClient.Described?> = MutableStateFlow(null)

    override suspend fun submitValues(
        requestId: String,
        values: Map<String, String>,
    ): TaffyResult<Unit> = TaffyResult.Failure(FailureReason.CORE_UNAVAILABLE)

    override suspend fun completeInteractive(requestId: String): TaffyResult<Unit> =
        TaffyResult.Failure(FailureReason.CORE_UNAVAILABLE)

    override fun close() = Unit
}

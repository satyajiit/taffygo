// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import com.taffygo.browser.ui.core.model.ProviderRoute
import com.taffygo.browser.ui.core.task.TaffyReadiness
import com.taffygo.browser.ui.core.task.TaffyReadinessRepository
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow

/** A readiness verdict a test states, and may change. */
internal class FixedReadiness(
    initial: TaffyReadiness = TaffyReadiness.Ready(ProviderRoute.DIRECT_WITH_YOUR_KEY),
) : TaffyReadinessRepository {
    private val verdict = MutableStateFlow(initial)
    override val readiness: StateFlow<TaffyReadiness> = verdict

    fun become(readiness: TaffyReadiness) {
        verdict.value = readiness
    }
}

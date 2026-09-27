// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.providers.internal

import com.taffygo.browser.ui.core.api.CoreApiClient
import com.taffygo.browser.ui.core.api.hasCompleteProjection
import com.taffygo.browser.ui.core.model.ThinkingLevel
import com.taffygo.browser.ui.core.providers.ProviderModelPreferences
import taffy.core_api.ThinkingLevelView

/**
 * The standing choice, handed to the browser in one command.
 *
 * A translation and nothing else: no local copy of the choice is kept, because
 * the accepted one arrives as the next roster row and a second copy here could
 * only ever disagree with it.
 */
internal class CoreProviderModelPreferences(
    private val core: CoreApiClient,
) : ProviderModelPreferences {

    override suspend fun choose(providerId: String, modelId: String?, thinking: ThinkingLevel?) {
        check(core.status.value.hasCompleteProjection()) {
            "Core projection is unavailable"
        }
        core.setProviderModelPreference(providerId, modelId, thinking?.toView())
    }
}

/**
 * The app's rung named in the contract's vocabulary.
 *
 * Total over a closed enumeration on purpose: a new rung is a compile failure
 * here rather than a silent fall back to a neighbouring amount of thinking.
 */
private fun ThinkingLevel.toView(): ThinkingLevelView = when (this) {
    ThinkingLevel.OFF -> ThinkingLevelView.OFF
    ThinkingLevel.MINIMAL -> ThinkingLevelView.MINIMAL
    ThinkingLevel.LOW -> ThinkingLevelView.LOW
    ThinkingLevel.MEDIUM -> ThinkingLevelView.MEDIUM
    ThinkingLevel.HIGH -> ThinkingLevelView.HIGH
    ThinkingLevel.XHIGH -> ThinkingLevelView.XHIGH
    ThinkingLevel.MAX -> ThinkingLevelView.MAX
}

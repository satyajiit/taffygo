// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.onboarding

import com.taffygo.browser.ui.core.model.ProviderRoute

/**
 * What the screen's primary action means, given what has been chosen.
 *
 * The primary action needs a route. It stays disabled until one is chosen,
 * and its label says what the tap will do. Skipping is a separate action with
 * its own words, never this one relabelled.
 */
internal enum class AiSetupFooterAction {
    /** Nothing is chosen; the button is disabled and says where it would go. */
    CONTINUE,

    /** Continue into provider and key setup. */
    CHOOSE_PROVIDER,
}

/**
 * What the primary action means for [state].
 *
 * Pure, and separate from the composable, so a host test can ask the question
 * without a device.
 */
internal fun aiSetupFooterAction(state: AiSetupUiState): AiSetupFooterAction =
    when (state.route) {
        ProviderRoute.NOT_CONFIGURED,
        ProviderRoute.NO_MODEL_REQUIRED,
        -> AiSetupFooterAction.CONTINUE
        ProviderRoute.DIRECT_WITH_YOUR_KEY -> AiSetupFooterAction.CHOOSE_PROVIDER
    }

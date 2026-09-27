// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.providers

import com.taffygo.browser.ui.core.model.ThinkingLevel

/**
 * The whole of what one press on screen SCR-417 stated.
 *
 * It is a record of a command that went out, never a claim about what stands.
 * The screen marks the row it names as *asked for* and goes on marking the
 * roster's own answer as the one in use, so a preference the core refuses is
 * visible as a request that got no reply rather than as a selection that took.
 *
 * All three fields travel together because the command does: a model and a
 * thinking level are one standing state, and either sent alone would clear the
 * other (`ProviderModelPreferences.choose`).
 */
data class ModelSelectionAsk(
    /** The provider the choice was filed under. */
    val providerId: String,
    /** The model asked for, null to fall back to the provider's own order. */
    val modelId: String?,
    /** The rung asked for, null for Auto — Taffy deciding, and never OFF. */
    val thinking: ThinkingLevel?,
)

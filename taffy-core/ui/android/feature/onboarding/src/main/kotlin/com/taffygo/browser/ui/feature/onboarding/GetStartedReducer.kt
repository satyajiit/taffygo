// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.onboarding

import com.taffygo.browser.ui.core.model.LocalProfile

/**
 * What typing and choosing do to screen SCR-007. Pure, so every rule below is
 * a unit test rather than a screenshot.
 *
 * A continue already in flight changes nothing further: the writes and the
 * navigation are one step, and a second tap part-way through would either
 * write twice or push AI setup twice.
 */
internal fun reduceGetStarted(
    state: GetStartedUiState,
    intent: GetStartedIntent,
): GetStartedUiState {
    if (state.continuing) return state
    // Exhaustive over the sealed interface on purpose: a new intent must be
    // a compile error here rather than a branch that quietly changes nothing.
    return when (intent) {
        is GetStartedIntent.EditName ->
            state.copy(name = LocalProfile.boundedDisplayName(intent.text))
        is GetStartedIntent.ChooseAvatar -> state.copy(avatar = intent.avatar)
        GetStartedIntent.OpenDataSheet -> state.copy(dataSheetOpen = true)
        GetStartedIntent.CloseDataSheet -> state.copy(dataSheetOpen = false)
        GetStartedIntent.Continue -> state.copy(continuing = true)
    }
}

/**
 * The name as it will be stored, or null for a person who gave none.
 *
 * Named separately from the field so the screen has one place that decides
 * what "gave no name" means, and so a blank-looking field of spaces reaches
 * the store as nothing rather than as a name made of spaces.
 */
internal fun getStartedStoredName(state: GetStartedUiState): String? =
    LocalProfile.normalizedDisplayName(state.name)

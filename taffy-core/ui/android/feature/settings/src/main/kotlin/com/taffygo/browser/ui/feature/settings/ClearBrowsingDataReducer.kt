// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

internal fun reduceClearBrowsingData(
    state: ClearBrowsingDataUiState,
    intent: ClearBrowsingDataIntent,
): ClearBrowsingDataUiState = when (intent) {
    is ClearBrowsingDataIntent.SelectRange ->
        if (state.submitting) state else state.copy(range = intent.range, failed = false)
    is ClearBrowsingDataIntent.ToggleClass -> {
        if (intent.dataClass !in state.supportedClasses || state.submitting) {
            state
        } else {
            val next = if (intent.dataClass in state.classes) {
                state.classes - intent.dataClass
            } else {
                state.classes + intent.dataClass
            }
            state.copy(classes = next, failed = false)
        }
    }
    ClearBrowsingDataIntent.Confirm ->
        if (state.canClear) state.copy(confirming = true, failed = false) else state
    ClearBrowsingDataIntent.DismissConfirm ->
        state.copy(confirming = false)
    ClearBrowsingDataIntent.Submit ->
        if (state.canClear) state.copy(submitting = true, failed = false) else state
    ClearBrowsingDataIntent.Dismiss -> state
}

// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.workspaces

import com.taffygo.browser.ui.core.model.FactId
import com.taffygo.browser.ui.core.model.Workspace

/** What screen SCR-307 shows, or the missing state when nothing resolved. */
internal fun projectFactCorrection(
    workspace: Workspace?,
    factId: FactId,
    loading: Boolean = false,
    unavailable: Boolean = false,
): FactCorrectionUiState {
    if (workspace != null) {
        val fact = workspace.facts.firstOrNull { it.id == factId }
        if (fact != null) {
            return FactCorrectionUiState(
                field = fact.field,
                pageValue = fact.value,
                enteredValue = fact.correction.orEmpty(),
                // Every other cell that shares a source with this one would be
                // recomputed, which is what the sheet warns about.
                downstreamCount = workspace.facts.count { other ->
                    other.id != fact.id && other.sources.any { it in fact.sources }
                },
            )
        }
    }
    return when {
        loading -> FactCorrectionUiState(loading = true)
        unavailable -> FactCorrectionUiState(unavailable = true)
        else -> FactCorrectionUiState(missing = true)
    }
}

/** What typing does to the sheet. */
internal fun reduceFactCorrection(
    state: FactCorrectionUiState,
    intent: FactCorrectionIntent,
): FactCorrectionUiState = when (intent) {
    is FactCorrectionIntent.ValueChanged -> state.copy(enteredValue = intent.value)
    FactCorrectionIntent.Save, FactCorrectionIntent.Cancel -> state
}

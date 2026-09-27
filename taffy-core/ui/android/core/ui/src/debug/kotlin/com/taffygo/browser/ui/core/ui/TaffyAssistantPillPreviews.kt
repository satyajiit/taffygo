// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.padding
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import com.taffygo.browser.ui.core.designsystem.TaffyTheme

/** Debug-only Compose previews; never part of the product APK. */
@ThemePreviews
@Composable
private fun TaffyAssistantPillLightPreview() {
    TaffyPreview(darkTheme = false) {
        TaffyAssistantPillStates()
    }
}

@ThemePreviews
@Composable
private fun TaffyAssistantPillDarkPreview() {
    TaffyPreview(darkTheme = true) {
        TaffyAssistantPillStates()
    }
}

/**
 * Every state, stacked the way the design document shows them.
 *
 * Driven off `entries` rather than a hand-written list, because a hand-written
 * list of four went on compiling — and went on looking complete — for the whole
 * time the bar had nine states and drew the other five somewhere else.
 */
@Composable
private fun TaffyAssistantPillStates() {
    Column(
        modifier = Modifier.padding(TaffyTheme.spacing.tight),
        verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.tight),
    ) {
        TaffyAssistantPillState.entries.forEach { state ->
            TaffyAssistantPill(
                state = state,
                label = previewLine(state),
                onClick = {},
                onAction = {},
            )
        }
    }
}

/** The spec's own copy for each state, verbatim where the spec fixes it. */
private fun previewLine(state: TaffyAssistantPillState): String = when (state) {
    TaffyAssistantPillState.IDLE -> ""
    TaffyAssistantPillState.RUNNING -> "Comparing 4 pages…"
    TaffyAssistantPillState.WAITING -> "Needs your OK — 2 tabs"
    TaffyAssistantPillState.PAUSED -> "Paused — 2 of 4 pages read"
    TaffyAssistantPillState.HELD -> "Taffy isn't running right now"
    TaffyAssistantPillState.DONE -> "Done — 3 sources, 1 conflict"
    TaffyAssistantPillState.PARTLY_DONE -> "Partly done — 3 pages read"
    TaffyAssistantPillState.STOPPED -> "You stopped Taffy"
    TaffyAssistantPillState.FAILED -> "Couldn't finish — your AI provider didn't respond"
}

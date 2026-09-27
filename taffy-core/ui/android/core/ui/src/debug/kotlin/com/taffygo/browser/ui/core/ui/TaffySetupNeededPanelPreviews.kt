// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.ui

import androidx.compose.foundation.layout.padding
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import com.taffygo.browser.ui.core.designsystem.TaffyTheme

// The panel's real sentences belong to the surfaces that know which one is
// true; this module has none, so the previews borrow its own labels.

@ThemePreviews
@Composable
private fun TaffySetupNeededPanelLightPreview() {
    TaffyPreview(darkTheme = false) {
        TaffySetupNeededPanelPreviewBody()
    }
}

@ThemePreviews
@Composable
private fun TaffySetupNeededPanelDarkPreview() {
    TaffyPreview(darkTheme = true) {
        TaffySetupNeededPanelPreviewBody()
    }
}

@FontScalePreviews
@Composable
private fun TaffySetupNeededPanelFontScalePreview() {
    TaffyPreview(darkTheme = false) {
        TaffySetupNeededPanelPreviewBody()
    }
}

@Composable
private fun TaffySetupNeededPanelPreviewBody() {
    TaffySetupNeededPanel(
        title = taffyString(R.string.taffy_assistant_ask_taffy),
        body = taffyString(R.string.taffy_fact_kind_needs_a_new_source),
        primaryLabel = taffyString(R.string.taffy_assistant_review),
        onPrimary = {},
        secondaryLabel = taffyString(R.string.taffy_action_back),
        onSecondary = {},
        modifier = Modifier.padding(horizontal = TaffyTheme.spacing.section),
    )
}

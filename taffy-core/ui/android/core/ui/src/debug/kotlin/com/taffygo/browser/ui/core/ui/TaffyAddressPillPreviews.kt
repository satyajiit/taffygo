// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.material3.Icon
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.taffygo.browser.ui.core.designsystem.TaffyBorders
import com.taffygo.browser.ui.core.designsystem.TaffyTheme

/** Debug-only Compose previews; never part of the product APK. */
@ThemePreviews
@Composable
private fun TaffyAddressPillLightPreview() {
    TaffyPreview(darkTheme = false) {
        TaffyAddressPillPreviewContent()
    }
}

@ThemePreviews
@Composable
private fun TaffyAddressPillDarkPreview() {
    TaffyPreview(darkTheme = true) {
        TaffyAddressPillPreviewContent()
    }
}

/**
 * The loading rail, in the one form a preview can show.
 *
 * Under reduced motion the rail is a still full-width bar, which renders. The
 * moving band does not: a preview has no frame clock, so `taffyPhase` never
 * advances past zero and the band sits one band-width off the left edge, where
 * it is invisible. Reviewing the travelling form needs a device.
 */
@ThemePreviews
@Composable
private fun TaffyAddressPillLoadingStillPreview() {
    TaffyPreview(darkTheme = false, reducedMotion = true) {
        TaffyAddressPillPreviewContent(isLoading = true)
    }
}

@ThemePreviews
@Composable
private fun TaffyAddressPillLoadingStillDarkPreview() {
    TaffyPreview(darkTheme = true, reducedMotion = true) {
        TaffyAddressPillPreviewContent(isLoading = true)
    }
}

@Composable
private fun TaffyAddressPillPreviewContent(isLoading: Boolean = false) {
    val url = "croma.com/tv"
    Box(modifier = Modifier.padding(TaffyTheme.spacing.tight)) {
        TaffyAddressPill(
            url = url,
            blockedCount = 12,
            isLoading = isLoading,
            isSecure = true,
            onReload = {},
            onStopLoading = {},
        )
    }
}

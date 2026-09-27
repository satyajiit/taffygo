// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.assistant

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.LiveRegionMode
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.liveRegion
import androidx.compose.ui.semantics.semantics
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import com.taffygo.browser.ui.core.designsystem.TaffyBorders
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.model.TaskControl
import com.taffygo.browser.ui.core.model.TaskDisplayState
import com.taffygo.browser.ui.core.ui.FontScalePreviews
import com.taffygo.browser.ui.core.ui.StatusPresentation
import com.taffygo.browser.ui.core.ui.TaffyAssistantPill
import com.taffygo.browser.ui.core.ui.TaffyAssistantPillState
import com.taffygo.browser.ui.core.ui.TaffyButtonSize
import com.taffygo.browser.ui.core.ui.TaffyControlBar
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyModeChip
import com.taffygo.browser.ui.core.ui.TaffyNavigator
import com.taffygo.browser.ui.core.ui.TaffyPreview
import com.taffygo.browser.ui.core.ui.TaffySecondaryButton
import com.taffygo.browser.ui.core.ui.TaffyStatusChip
import com.taffygo.browser.ui.core.ui.ThemePreviews
import com.taffygo.browser.ui.core.ui.screenViewModel
import com.taffygo.browser.ui.core.ui.taffyString

/** Debug-only Compose previews; never part of the product APK. */
@ThemePreviews
@Composable
private fun AssistantBarRunningPreview() {
    TaffyPreview(darkTheme = false) {
        AssistantBarContent(state = AssistantPreviewStates.running, onIntent = {})
    }
}

@ThemePreviews
@Composable
private fun AssistantBarDarkPreview() {
    TaffyPreview(darkTheme = true) {
        AssistantBarContent(state = AssistantPreviewStates.waitingForYou, onIntent = {})
    }
}

/** The bar beside a task nothing is driving: no rail, and no claim. */
@ThemePreviews
@Composable
private fun AssistantBarNotDrivenPreview() {
    TaffyPreview(darkTheme = false) {
        AssistantBarContent(state = AssistantPreviewStates.runningNotDriven, onIntent = {})
    }
}

@FontScalePreviews
@Composable
private fun AssistantBarPartlyDonePreview() {
    TaffyPreview(darkTheme = false) {
        AssistantBarContent(state = AssistantPreviewStates.partlyDone, onIntent = {})
    }
}

@ThemePreviews
@Composable
private fun AssistantBarDonePreview() {
    TaffyPreview(darkTheme = false) {
        AssistantBarContent(state = AssistantPreviewStates.done, onIntent = {})
    }
}

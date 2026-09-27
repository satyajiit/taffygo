// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.assistant

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.ui.FontScalePreviews
import com.taffygo.browser.ui.core.ui.StatusPresentation
import com.taffygo.browser.ui.core.ui.TaffyControlBar
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyEmptyState
import com.taffygo.browser.ui.core.ui.TaffyModeChip
import com.taffygo.browser.ui.core.ui.TaffyNavigator
import com.taffygo.browser.ui.core.ui.TaffyPreview
import com.taffygo.browser.ui.core.ui.TaffyPrimaryButton
import com.taffygo.browser.ui.core.ui.TaffyScreen
import com.taffygo.browser.ui.core.ui.TaffySecondaryButton
import com.taffygo.browser.ui.core.ui.TaffySectionHeader
import com.taffygo.browser.ui.core.ui.TaffyStatusChip
import com.taffygo.browser.ui.core.ui.ThemePreviews
import com.taffygo.browser.ui.core.ui.screenViewModel
import com.taffygo.browser.ui.core.ui.taffyPlural
import com.taffygo.browser.ui.core.ui.taffyString

/** Debug-only Compose previews; never part of the product APK. */
@ThemePreviews
@Composable
private fun TaskViewRunningPreview() {
    TaffyPreview(darkTheme = false) {
        TaskViewContent(state = AssistantPreviewStates.taskRunning, onIntent = {})
    }
}

@ThemePreviews
@Composable
private fun TaskViewDarkPreview() {
    TaffyPreview(darkTheme = true) {
        TaskViewContent(state = AssistantPreviewStates.taskPartlyDone, onIntent = {})
    }
}

/** The screen the defect produced, with the sentence it was missing. */
@ThemePreviews
@Composable
private fun TaskViewNotDrivenPreview() {
    TaffyPreview(darkTheme = false) {
        TaskViewContent(state = AssistantPreviewStates.taskNotDriven, onIntent = {})
    }
}

/** A press the task machine refused, with the answer it used to swallow. */
@ThemePreviews
@Composable
private fun TaskViewRefusedPreview() {
    TaffyPreview(darkTheme = false) {
        TaskViewContent(state = AssistantPreviewStates.taskRefused, onIntent = {})
    }
}

@FontScalePreviews
@Composable
private fun TaskViewWaitingPreview() {
    TaffyPreview(darkTheme = false) {
        TaskViewContent(state = AssistantPreviewStates.taskWaiting, onIntent = {})
    }
}

@ThemePreviews
@Composable
private fun TaskViewFailedProviderPreview() {
    TaffyPreview(darkTheme = false) {
        TaskViewContent(state = AssistantPreviewStates.taskFailedProvider, onIntent = {})
    }
}

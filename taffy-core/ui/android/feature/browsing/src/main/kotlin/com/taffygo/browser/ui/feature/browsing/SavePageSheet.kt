// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import com.taffygo.browser.ui.core.designsystem.TaffySkeleton
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.ui.TaffyBottomSheet
import com.taffygo.browser.ui.core.ui.TaffyPrimaryButton
import com.taffygo.browser.ui.core.ui.TaffySecondaryButton
import com.taffygo.browser.ui.core.ui.taffyString

/**
 * Save page (SCR-811): star the page in front of you.
 *
 * A local overlay on SCR-101, not a destination. Save stays disabled until a
 * writer exists; a private tab refuses in words and never writes.
 */
@Composable
internal fun SavePageSheet(
    state: SavePageUiState,
    onIntent: (SavePageIntent) -> Unit,
) {
    TaffyBottomSheet(
        title = taffyString(R.string.taffy_save_page_title),
        onDismissRequest = { onIntent(SavePageIntent.Dismiss) },
        testTag = SAVE_PAGE_TEST_TAG,
    ) {
        if (state.title.isNotBlank()) {
            Text(
                text = state.title,
                style = TaffyTheme.typography.title,
                color = TaffyTheme.colors.textPrimary,
            )
        }
        if (state.host.isNotBlank()) {
            Text(
                text = state.host,
                style = TaffyTheme.typography.detail,
                color = TaffyTheme.colors.textSecondary,
            )
        }
        val body = when {
            state.isPrivate -> taffyString(R.string.taffy_save_page_private)
            state.canonicalUrl.isBlank() -> taffyString(R.string.taffy_save_page_unavailable)
            !state.canSave -> taffyString(R.string.taffy_save_page_unavailable)
            state.saveStatus == SavePageUiState.SaveStatus.FAILED ->
                taffyString(R.string.taffy_save_page_failed)
            else -> null
        }
        if (body != null) {
            Text(
                text = body,
                style = TaffyTheme.typography.body,
                color = TaffyTheme.colors.textPrimary,
                modifier = Modifier.testTag(SAVE_PAGE_BODY_TEST_TAG),
            )
        }
        if (state.loading || state.saveStatus == SavePageUiState.SaveStatus.SAVING) {
            TaffySkeleton(
                modifier = Modifier
                    .fillMaxWidth()
                    .height(TaffyTheme.spacing.minimumTouchTarget),
                accessibleDescription = taffyString(R.string.taffy_browser_loading),
            )
        }
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.snug),
        ) {
            TaffySecondaryButton(
                label = taffyString(R.string.taffy_save_page_cancel),
                onClick = { onIntent(SavePageIntent.Dismiss) },
                modifier = Modifier.weight(1f),
                testTag = SAVE_PAGE_CANCEL_TEST_TAG,
            )
            TaffyPrimaryButton(
                label = taffyString(R.string.taffy_save_page_save),
                onClick = { onIntent(SavePageIntent.Save) },
                enabled = state.primaryEnabled,
                modifier = Modifier.weight(1f),
                testTag = SAVE_PAGE_SAVE_TEST_TAG,
            )
        }
    }
}

internal fun SavePageIntent.toBrowserMainIntent(): BrowserMainIntent = when (this) {
    is SavePageIntent.ChooseFolder -> BrowserMainIntent.ChooseSaveFolder(folderId)
    SavePageIntent.Save -> BrowserMainIntent.ConfirmSavePage
    SavePageIntent.Dismiss -> BrowserMainIntent.DismissSavePage
}

/** The save-page sheet, which screen SCR-811's semantics tests name. */
const val SAVE_PAGE_TEST_TAG: String = "save_page_sheet"

/** The honest body: private refusal, or this build cannot save. */
const val SAVE_PAGE_BODY_TEST_TAG: String = "save_page_body"

/** Star the page. Disabled until a writer exists on a non-private tab. */
const val SAVE_PAGE_SAVE_TEST_TAG: String = "save_page_save"

/** Close without saving. */
const val SAVE_PAGE_CANCEL_TEST_TAG: String = "save_page_cancel"

// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import android.text.format.DateUtils
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.ui.TaffyDangerButton
import com.taffygo.browser.ui.core.ui.TaffySecondaryButton
import com.taffygo.browser.ui.core.ui.taffyString

/** Metadata for one saved sign-in. Passwords cannot enter this surface. */
@Composable
internal fun SavedSignInDetail(
    state: SavedSignInsUiState,
    onIntent: (SavedSignInsIntent) -> Unit,
) {
    val record = state.opened ?: return
    Text(
        text = record.site,
        style = TaffyTheme.typography.title,
        color = TaffyTheme.colors.textPrimary,
        modifier = Modifier.testTag(SIGN_IN_DETAIL_SITE_TEST_TAG),
    )
    SavedMetadataRow(
        label = taffyString(R.string.taffy_sign_ins_username),
        value = record.username,
        testTag = SIGN_IN_USERNAME_TEST_TAG,
    )
    if (record.lastUsedEpochMillis > 0L) {
        SavedMetadataRow(
            label = taffyString(R.string.taffy_sign_ins_last_used_label),
            value = DateUtils.getRelativeTimeSpanString(record.lastUsedEpochMillis).toString(),
            testTag = SIGN_IN_LAST_USED_TEST_TAG,
        )
    }
    if (state.confirmDelete) {
        Text(
            text = taffyString(R.string.taffy_sign_ins_delete_title, record.site),
            style = TaffyTheme.typography.title,
            color = TaffyTheme.colors.textPrimary,
        )
        Text(
            text = taffyString(R.string.taffy_sign_ins_delete_body),
            style = TaffyTheme.typography.body,
            color = TaffyTheme.colors.textSecondary,
        )
        TaffyDangerButton(
            label = taffyString(R.string.taffy_sign_ins_delete_confirm),
            onClick = { onIntent(SavedSignInsIntent.ConfirmDelete) },
            modifier = Modifier.fillMaxWidth(),
            testTag = SIGN_IN_DELETE_CONFIRM_TEST_TAG,
        )
        TaffySecondaryButton(
            label = taffyString(R.string.taffy_sign_ins_delete_keep),
            onClick = { onIntent(SavedSignInsIntent.CancelDelete) },
            modifier = Modifier.fillMaxWidth(),
        )
    } else {
        TaffyDangerButton(
            label = taffyString(R.string.taffy_sign_ins_delete),
            onClick = { onIntent(SavedSignInsIntent.Delete) },
            modifier = Modifier.fillMaxWidth(),
            testTag = SIGN_IN_DELETE_TEST_TAG,
        )
    }
}

@Composable
private fun SavedMetadataRow(label: String, value: String, testTag: String) {
    Column(
        modifier = Modifier.fillMaxWidth(),
        verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.tight),
    ) {
        Text(
            text = label,
            style = TaffyTheme.typography.label,
            color = TaffyTheme.colors.textSecondary,
        )
        Text(
            text = value,
            style = TaffyTheme.typography.body,
            color = TaffyTheme.colors.textPrimary,
            modifier = Modifier
                .fillMaxWidth()
                .clip(TaffyTheme.shapes.row)
                .background(TaffyTheme.colors.surfaceSunken)
                .heightIn(min = TaffyTheme.spacing.minimumTouchTarget)
                .padding(TaffyTheme.spacing.snug)
                .testTag(testTag)
                .semantics { contentDescription = value },
        )
    }
}

const val SIGN_IN_DETAIL_SITE_TEST_TAG: String = "saved_sign_in_site"
const val SIGN_IN_USERNAME_TEST_TAG: String = "saved_sign_in_username"
const val SIGN_IN_LAST_USED_TEST_TAG: String = "saved_sign_in_last_used"
const val SIGN_IN_DELETE_TEST_TAG: String = "saved_sign_in_delete"
const val SIGN_IN_DELETE_CONFIRM_TEST_TAG: String = "saved_sign_in_delete_confirm"

// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import androidx.compose.foundation.lazy.LazyListScope
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import com.taffygo.browser.ui.core.browser.BrowserProfilesRepository
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.ui.TaffyBottomSheet
import com.taffygo.browser.ui.core.ui.TaffyDangerButton
import com.taffygo.browser.ui.core.ui.TaffySecondaryButton
import com.taffygo.browser.ui.core.ui.taffyString

internal fun LazyListScope.profileStatus(
    state: BrowserProfilesUiState,
    onIntent: (BrowserProfilesIntent) -> Unit,
) {
    state.operation?.let { operation ->
        item(key = "profiles-operation", contentType = "status") {
            Text(
                text = taffyString(operation.messageResource()),
                style = TaffyTheme.typography.detail,
                color = TaffyTheme.colors.textSecondary,
                modifier = Modifier.testTag(BROWSER_PROFILES_STATUS_TEST_TAG),
            )
        }
    }
    state.failure?.let { failure ->
        item(key = "profiles-failure", contentType = "status") {
            Text(
                text = taffyString(failure.messageResource()),
                style = TaffyTheme.typography.detail,
                color = TaffyTheme.colors.danger,
                modifier = Modifier.testTag(BROWSER_PROFILES_ERROR_TEST_TAG),
            )
            TaffySecondaryButton(
                label = taffyString(R.string.taffy_profiles_dismiss_error),
                onClick = { onIntent(BrowserProfilesIntent.DismissFailure) },
            )
        }
    }
}

@Composable
internal fun DeleteProfileSheet(
    state: BrowserProfilesUiState,
    onIntent: (BrowserProfilesIntent) -> Unit,
) {
    val candidate = state.deleteCandidate ?: return
    TaffyBottomSheet(
        title = taffyString(R.string.taffy_profiles_delete_title, candidate.displayName),
        onDismissRequest = { onIntent(BrowserProfilesIntent.DismissDelete) },
        testTag = BROWSER_PROFILES_DELETE_SHEET_TEST_TAG,
    ) {
        Text(
            text = taffyString(R.string.taffy_profiles_delete_body),
            style = TaffyTheme.typography.detail,
            color = TaffyTheme.colors.textSecondary,
        )
        TaffyDangerButton(
            label = taffyString(R.string.taffy_profiles_delete),
            onClick = { onIntent(BrowserProfilesIntent.ConfirmDelete) },
            enabled = !state.busy,
            testTag = BROWSER_PROFILES_CONFIRM_DELETE_TEST_TAG,
        )
        TaffySecondaryButton(
            label = taffyString(R.string.taffy_profiles_keep),
            onClick = { onIntent(BrowserProfilesIntent.DismissDelete) },
            enabled = !state.busy,
        )
    }
}

private fun BrowserProfilesUiState.Operation.messageResource(): Int = when (this) {
    BrowserProfilesUiState.Operation.DELETING -> R.string.taffy_profiles_deleting
}

internal fun BrowserProfilesRepository.Failure.messageResource(): Int = when (this) {
    BrowserProfilesRepository.Failure.INVALID_NAME -> R.string.taffy_profiles_error_name
    BrowserProfilesRepository.Failure.LIMIT_REACHED -> R.string.taffy_profiles_error_limit
    BrowserProfilesRepository.Failure.DUPLICATE_NAME -> R.string.taffy_profiles_error_duplicate
    BrowserProfilesRepository.Failure.NOT_FOUND -> R.string.taffy_profiles_error_missing
    BrowserProfilesRepository.Failure.ACTIVE_PROFILE -> R.string.taffy_profiles_error_active
    BrowserProfilesRepository.Failure.LAST_PROFILE -> R.string.taffy_profiles_error_last
    BrowserProfilesRepository.Failure.PROFILE_IN_USE -> R.string.taffy_profiles_error_in_use
    BrowserProfilesRepository.Failure.BUSY -> R.string.taffy_profiles_error_busy
    BrowserProfilesRepository.Failure.PRIVATE_PROFILE -> R.string.taffy_profiles_error_private
    BrowserProfilesRepository.Failure.NOT_ACTIVE,
    BrowserProfilesRepository.Failure.UNAVAILABLE,
    BrowserProfilesRepository.Failure.FAILED,
    -> R.string.taffy_profiles_error_failed
}

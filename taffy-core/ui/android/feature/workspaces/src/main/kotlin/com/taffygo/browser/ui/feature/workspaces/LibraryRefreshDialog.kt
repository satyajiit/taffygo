// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.workspaces

import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.unit.dp
import com.taffygo.browser.ui.core.ui.TaffyPrimaryButton
import com.taffygo.browser.ui.core.ui.TaffySecondaryButton
import com.taffygo.browser.ui.core.ui.taffyPlural
import com.taffygo.browser.ui.core.ui.taffyString

/** Content-free approval shown before any page is revisited. */
@Composable
internal fun LibraryRefreshDialog(
    preview: LibraryRepository.RefreshPreview,
    onDismiss: () -> Unit,
    onConfirm: () -> Unit,
) {
    AlertDialog(
        modifier = Modifier.testTag(LIBRARY_REFRESH_DIALOG_TEST_TAG),
        onDismissRequest = onDismiss,
        title = { Text(taffyString(R.string.taffy_library_refresh_title)) },
        text = {
            Column(Modifier.verticalScroll(rememberScrollState())) {
                Text(
                    taffyString(
                        R.string.taffy_library_refresh_work,
                        taffyPlural(
                            R.plurals.taffy_library_refresh_navigation_count,
                            preview.navigationCount.toInt(),
                            preview.navigationCount.toLong(),
                        ),
                        taffyPlural(
                            R.plurals.taffy_library_refresh_observation_count,
                            preview.observationCount.toInt(),
                            preview.observationCount.toLong(),
                        ),
                    ),
                )
                Text(taffyString(R.string.taffy_library_refresh_no_provider))
                Spacer(Modifier.height(12.dp))
                Text(taffyString(R.string.taffy_library_refresh_sources))
                preview.sources.forEach { source ->
                    Text(
                        taffyString(
                            R.string.taffy_library_refresh_source,
                            source.title,
                            source.host,
                        ),
                    )
                }
            }
        },
        confirmButton = {
            TaffyPrimaryButton(
                label = taffyString(R.string.taffy_library_refresh_confirm),
                onClick = onConfirm,
                testTag = LIBRARY_REFRESH_CONFIRM_TEST_TAG,
            )
        },
        dismissButton = {
            TaffySecondaryButton(
                label = taffyString(R.string.taffy_workspace_lifecycle_cancel),
                onClick = onDismiss,
            )
        },
    )
}

const val LIBRARY_REFRESH_DIALOG_TEST_TAG: String = "library_refresh_dialog"
const val LIBRARY_REFRESH_CONFIRM_TEST_TAG: String = "library_refresh_confirm"

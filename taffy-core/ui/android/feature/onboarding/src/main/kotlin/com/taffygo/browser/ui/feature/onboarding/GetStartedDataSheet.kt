// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.onboarding

import android.content.Context
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.produceState
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.testTag
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.ui.LocalAppDispatchers
import com.taffygo.browser.ui.core.ui.TaffyBottomSheet
import com.taffygo.browser.ui.core.ui.taffyString
import kotlinx.coroutines.withContext

/**
 * What this product does with a person's data, said in the product.
 *
 * It is a sheet rather than a link because there is nowhere to link to:
 * TaffyGo operates no host (decision 0200), and the screen this replaced
 * pointed at two pages on a website that this repository neither deploys nor
 * controls. An answer a person can read with no network is also the only
 * answer that is available at the moment they are asked to decide.
 */
@Composable
internal fun GetStartedDataSheet(onDismiss: () -> Unit) {
    // Package metadata is Android presentation detail and never crosses Core API.
    val context = LocalContext.current.applicationContext
    val ioDispatcher = LocalAppDispatchers.current.io
    val versionName by produceState(initialValue = "", key1 = context, key2 = ioDispatcher) {
        value = withContext(ioDispatcher) { installedVersionName(context) }
    }
    TaffyBottomSheet(
        title = taffyString(R.string.taffy_get_started_data_title),
        onDismissRequest = onDismiss,
        testTag = GET_STARTED_DATA_SHEET_TEST_TAG,
    ) {
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = TaffyTheme.spacing.screenMargin),
            verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.snug),
        ) {
            DataParagraph(R.string.taffy_get_started_data_device)
            DataParagraph(R.string.taffy_get_started_data_provider)
            DataParagraph(R.string.taffy_get_started_data_sites)
            if (versionName.isNotEmpty()) {
                Text(
                    text = taffyString(R.string.taffy_get_started_version, versionName),
                    style = TaffyTheme.typography.micro,
                    color = TaffyTheme.colors.textTertiary,
                    modifier = Modifier
                        .fillMaxWidth()
                        .testTag(GET_STARTED_VERSION_TEST_TAG),
                )
            }
        }
    }
}

@Composable
private fun DataParagraph(text: Int) {
    Text(
        text = taffyString(text),
        style = TaffyTheme.typography.body,
        color = TaffyTheme.colors.textSecondary,
    )
}

const val GET_STARTED_DATA_SHEET_TEST_TAG: String = "get_started_data_sheet"
const val GET_STARTED_VERSION_TEST_TAG: String = "get_started_version"

/**
 * The installed version, or nothing.
 *
 * `versionName` is platform-nullable and the sheet draws no line for an empty
 * one, so a build whose metadata cannot be read says nothing rather than
 * showing a version it does not know.
 */
@Suppress("DEPRECATION")
private fun installedVersionName(context: Context): String =
    runCatching { context.packageManager.getPackageInfo(context.packageName, 0).versionName }
        .getOrNull()
        .orEmpty()

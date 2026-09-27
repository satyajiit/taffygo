// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.ui.TaffyBrandLockup
import com.taffygo.browser.ui.core.ui.TaffyObjectCard
import com.taffygo.browser.ui.core.ui.taffyString

/** The TaffyGo lockup. A missing version is not drawn here. */
@Composable
internal fun AboutLockupCard() {
    TaffyObjectCard(testTag = ABOUT_LIST_TEST_TAG) {
        Box(
            modifier = Modifier.fillMaxWidth(),
            contentAlignment = Alignment.CenterStart,
        ) {
            TaffyBrandLockup(
                height = LockupHeight,
                contentDescription = taffyString(R.string.taffy_about_title),
            )
        }
    }
}

/** One version or Chromium fact. Never a guessed number. */
@Composable
internal fun AboutFactCard(heading: String, body: String, testTag: String) {
    TaffyObjectCard(testTag = testTag) {
        Column(
            modifier = Modifier.fillMaxWidth(),
            verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.tight),
        ) {
            Text(
                text = heading,
                style = TaffyTheme.typography.micro,
                color = TaffyTheme.colors.textSecondary,
            )
            Text(
                text = body,
                style = TaffyTheme.typography.title,
                color = TaffyTheme.colors.textPrimary,
            )
        }
    }
}

private val LockupHeight = 48.dp

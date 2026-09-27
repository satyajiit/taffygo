// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.size
import androidx.compose.material3.Icon
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.semantics
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.ui.TaffyEmptyState
import com.taffygo.browser.ui.core.ui.TaffyIcon
import com.taffygo.browser.ui.core.ui.TaffyInfoTile
import com.taffygo.browser.ui.core.ui.taffyString

/** Transport fail: recedes. Not the empty card. */
@Composable
internal fun WhatHappenedUnavailableCard() {
    TaffyInfoTile(testTag = WHAT_UNAVAILABLE_TEST_TAG) {
        Column(
            modifier = Modifier.fillMaxWidth(),
            verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.tight),
        ) {
            Text(
                text = taffyString(R.string.taffy_happened_unavailable_title),
                style = TaffyTheme.typography.title,
                color = TaffyTheme.colors.textPrimary,
                modifier = Modifier.semantics { heading() },
            )
            Text(
                text = taffyString(R.string.taffy_happened_unavailable_body),
                style = TaffyTheme.typography.detail,
                color = TaffyTheme.colors.textSecondary,
            )
        }
    }
}

/** No terminal workspace snapshots are currently available. */
@Composable
internal fun WhatHappenedEmptyCard() {
    Column(modifier = Modifier.fillMaxWidth().testTag(WHAT_EMPTY_TEST_TAG)) {
        TaffyEmptyState(
            title = taffyString(R.string.taffy_happened_empty_title),
            body = taffyString(R.string.taffy_happened_empty_body),
            leading = {
                Icon(
                    imageVector = TaffyIcon.Newspaper,
                    contentDescription = null,
                    tint = TaffyTheme.colors.textPrimary,
                    modifier = Modifier.size(SettingsGlyphSize),
                )
            },
        )
    }
}

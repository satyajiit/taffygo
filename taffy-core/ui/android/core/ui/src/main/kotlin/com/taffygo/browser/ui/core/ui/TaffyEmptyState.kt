// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.semantics
import com.taffygo.browser.ui.core.designsystem.TaffyTheme

/**
 * Nothing here yet, said plainly.
 *
 * Empty is a state the screen renders, not a blank area it leaves behind
 * (android-app-architecture section 4). [leading] is the glyph; this wraps it
 * in a 64 dp sunken well. [illustration] is a larger cutout (Taffy, never a
 * second mascot) that replaces the well when both would otherwise draw.
 */
@Composable
fun TaffyEmptyState(
    title: String,
    body: String,
    modifier: Modifier = Modifier,
    leading: @Composable (() -> Unit)? = null,
    action: @Composable (() -> Unit)? = null,
    illustration: @Composable (() -> Unit)? = null,
) {
    Column(
        modifier = modifier
            .fillMaxWidth()
            .padding(vertical = TaffyTheme.spacing.section)
            .testTag(EMPTY_STATE_TEST_TAG),
        verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.tight),
    ) {
        if (illustration != null) {
            illustration()
        } else if (leading != null) {
            TaffyGlyphFrame(size = TaffyGlyphFrameLargeSize) {
                leading()
            }
        }
        Text(
            text = title,
            style = TaffyTheme.typography.title,
            color = TaffyTheme.colors.textPrimary,
            modifier = Modifier.semantics { heading() },
        )
        Text(
            text = body,
            style = TaffyTheme.typography.detail,
            color = TaffyTheme.colors.textSecondary,
        )
        action?.invoke()
    }
}

/** The tag an empty state carries, so a semantics test names it once. */
const val EMPTY_STATE_TEST_TAG: String = "empty_state"

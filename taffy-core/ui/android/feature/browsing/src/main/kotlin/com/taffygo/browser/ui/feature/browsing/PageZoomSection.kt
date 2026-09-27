// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.ui.TaffyButtonSize
import com.taffygo.browser.ui.core.ui.TaffyGroupedCard
import com.taffygo.browser.ui.core.ui.TaffyIcon
import com.taffygo.browser.ui.core.ui.TaffyIconButton
import com.taffygo.browser.ui.core.ui.TaffySecondaryButton
import com.taffygo.browser.ui.core.ui.TaffySectionHeader
import com.taffygo.browser.ui.core.ui.taffyString

/** Persistent per-site layout zoom for the selected page. */
@Composable
internal fun PageZoomSection(
    state: PageZoomState,
    onIntent: (BrowserMainIntent) -> Unit,
) {
    Column(
        modifier = Modifier.fillMaxWidth(),
        verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.snug),
    ) {
        TaffySectionHeader(title = taffyString(R.string.taffy_page_zoom_title))
        TaffyGroupedCard(testTag = PAGE_ZOOM_TEST_TAG) {
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(TaffyTheme.spacing.snug),
                horizontalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.tight),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                TaffyIconButton(
                    icon = TaffyIcon.Minus,
                    contentDescription = taffyString(R.string.taffy_page_zoom_smaller),
                    onClick = { onIntent(BrowserMainIntent.ZoomPageOut) },
                    enabled = state.available && state.canZoomOut,
                    size = TaffyButtonSize.COMPACT,
                    testTag = PAGE_ZOOM_OUT_TEST_TAG,
                )
                val resetDescription = taffyString(
                    R.string.taffy_page_zoom_reset_description,
                    state.percent,
                )
                TaffySecondaryButton(
                    label = taffyString(R.string.taffy_page_zoom_percent, state.percent),
                    onClick = { onIntent(BrowserMainIntent.ResetPageZoom) },
                    modifier = Modifier
                        .weight(1f)
                        .semantics { contentDescription = resetDescription },
                    enabled = state.available && state.canReset,
                    size = TaffyButtonSize.COMPACT,
                    testTag = PAGE_ZOOM_RESET_TEST_TAG,
                )
                TaffyIconButton(
                    icon = TaffyIcon.Plus,
                    contentDescription = taffyString(R.string.taffy_page_zoom_larger),
                    onClick = { onIntent(BrowserMainIntent.ZoomPageIn) },
                    enabled = state.available && state.canZoomIn,
                    size = TaffyButtonSize.COMPACT,
                    testTag = PAGE_ZOOM_IN_TEST_TAG,
                )
            }
            if (!state.available) {
                Text(
                    text = taffyString(R.string.taffy_page_zoom_unavailable),
                    modifier = Modifier.padding(
                        start = TaffyTheme.spacing.screenMargin,
                        end = TaffyTheme.spacing.screenMargin,
                        bottom = TaffyTheme.spacing.snug,
                    ),
                    style = TaffyTheme.typography.detail,
                    color = TaffyTheme.colors.textSecondary,
                )
            }
        }
    }
}

const val PAGE_ZOOM_TEST_TAG: String = "page_zoom"
const val PAGE_ZOOM_OUT_TEST_TAG: String = "page_zoom_out"
const val PAGE_ZOOM_RESET_TEST_TAG: String = "page_zoom_reset"
const val PAGE_ZOOM_IN_TEST_TAG: String = "page_zoom_in"

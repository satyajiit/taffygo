// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.assistant

import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.key
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.selected
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import com.taffygo.browser.ui.core.browser.TabArtwork
import com.taffygo.browser.ui.core.designsystem.TaffyBorders
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.model.Tab
import com.taffygo.browser.ui.core.model.TabId
import com.taffygo.browser.ui.core.ui.TaffySectionHeader
import com.taffygo.browser.ui.core.ui.taffyPlural
import com.taffygo.browser.ui.core.ui.taffyString

/** A pair of readable page cards per row; opening one keeps the workspace on the back stack. */
@Composable
internal fun TaskBrowserPanel(
    state: TaskBrowserUiState,
    onOpenTab: (TabId) -> Unit,
    modifier: Modifier = Modifier,
) {
    if (state.tabs.isEmpty()) return
    Column(
        modifier = modifier.fillMaxWidth().testTag(TASK_BROWSER_PANEL_TEST_TAG),
        verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.snug),
    ) {
        TaffySectionHeader(taffyPlural(R.plurals.taffy_task_browser_pages, state.tabs.size, state.tabs.size))
        BoxWithConstraints(modifier = Modifier.fillMaxWidth()) {
            val columns = if (maxWidth < 300.dp * LocalDensity.current.fontScale) 1 else 2
            Column(verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.snug)) {
                state.tabs.chunked(columns).forEach { row ->
                    Row(horizontalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.snug)) {
                        row.forEach { tab ->
                            key(tab.id) {
                                TaskBrowserCard(
                                    tab = tab,
                                    artwork = state.artwork[tab.id],
                                    onOpen = { onOpenTab(tab.id) },
                                    modifier = Modifier.weight(1f),
                                )
                            }
                        }
                        if (row.size < columns) Spacer(Modifier.weight(1f))
                    }
                }
            }
        }
    }
}

@Composable
private fun TaskBrowserCard(
    tab: Tab,
    artwork: TabArtwork?,
    onOpen: () -> Unit,
    modifier: Modifier = Modifier,
) {
    val colors = TaffyTheme.colors
    val tone = when (Math.floorMod(tab.id.value.hashCode(), 3)) {
        0 -> colors.ribbonOneWash
        1 -> colors.ribbonTwoWash
        else -> colors.ribbonThreeWash
    }
    val title = tab.title.ifBlank {
        tab.host.ifBlank { taffyString(R.string.taffy_task_browser_page) }
    }
    val openLabel = taffyString(R.string.taffy_task_browser_open, title)
    Column(
        modifier = modifier
            .clip(TaffyTheme.shapes.card)
            .background(colors.surfaceRaised)
            .then(if (tab.isSelected) Modifier.border(TaffyBorders.standard, colors.outline, TaffyTheme.shapes.card) else Modifier)
            .clickable(role = Role.Button, onClickLabel = openLabel, onClick = onOpen)
            .semantics { selected = tab.isSelected }
            .testTag("$TASK_BROWSER_TAB_TEST_TAG_PREFIX${tab.id.value}"),
    ) {
        TaskPagePicture(tab = tab, artwork = artwork, tone = tone)
        Column(
            modifier = Modifier.padding(TaffyTheme.spacing.tight),
            verticalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.step),
        ) {
            Text(
                text = title,
                style = TaffyTheme.typography.label,
                color = colors.textPrimary,
                maxLines = 2,
                overflow = TextOverflow.Ellipsis,
            )
            if (tab.host.isNotBlank()) {
                Text(
                    text = tab.host,
                    style = TaffyTheme.typography.caption,
                    color = colors.textSecondary,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                )
            }
            Text(
                text = taffyString(R.string.taffy_task_browser_open_short),
                style = TaffyTheme.typography.caption,
                color = colors.textPrimary,
            )
        }
    }
}

/** These bitmaps were already drawn by the browser; this surface never requests a screenshot. */
@Composable
private fun TaskPagePicture(tab: Tab, artwork: TabArtwork?, tone: Color) {
    Box(
        modifier = Modifier.fillMaxWidth().aspectRatio(1.8f).background(tone)
            .testTag("$TASK_BROWSER_PICTURE_TEST_TAG_PREFIX${tab.id.value}"),
        contentAlignment = Alignment.Center,
    ) {
        val thumbnail = artwork?.thumbnail
        val favicon = artwork?.favicon
        when {
            thumbnail != null -> Image(
                bitmap = thumbnail.asImageBitmap(),
                contentDescription = null,
                contentScale = ContentScale.Crop,
                alignment = Alignment.TopCenter,
                modifier = Modifier.fillMaxSize(),
            )
            favicon != null -> Image(
                bitmap = favicon.asImageBitmap(),
                contentDescription = null,
                modifier = Modifier.size(40.dp),
            )
            else -> {
                val initial = tab.host.firstOrNull()?.uppercase().orEmpty()
                Text(
                    text = initial,
                    style = TaffyTheme.typography.display,
                    color = TaffyTheme.colors.textSecondary,
                )
            }
        }
    }
}

const val TASK_BROWSER_PANEL_TEST_TAG: String = "task_browser_panel"
const val TASK_BROWSER_TAB_TEST_TAG_PREFIX: String = "task_browser_tab_"
const val TASK_BROWSER_PICTURE_TEST_TAG_PREFIX: String = "task_browser_picture_"

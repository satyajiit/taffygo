// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.widthIn
import androidx.compose.material3.Icon
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import com.taffygo.browser.ui.core.designsystem.TaffyBorders
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.ui.TaffyGlyphFrame
import com.taffygo.browser.ui.core.ui.TaffyIcon
import com.taffygo.browser.ui.core.ui.taffyString

/**
 * One page on the question, as a chip in the row under the box.
 *
 * The same height as the shape and store chips beside it, because it is the
 * same kind of thing: a note on the question, not a control competing with
 * the field above. It carries the page's mark, its title and a remove; a
 * closed tab keeps its title in caution, shows the warning glyph, and says so
 * when read aloud; a page Taffy opened takes the accent edge and no remove,
 * because it is Taffy's to close.
 */
@Composable
internal fun StartPagePageChip(
    page: AttachedPage,
    onRemove: (() -> Unit)?,
    modifier: Modifier = Modifier,
) {
    val colors = TaffyTheme.colors
    val titleLine = page.title.ifBlank { page.host }
    val caption = when {
        page.closed -> taffyString(R.string.taffy_ask_tab_closed)
        page.taffyOpened -> taffyString(R.string.taffy_ask_taffy_opened)
        page.title.isNotBlank() -> page.host
        else -> ""
    }
    val spoken = if (caption.isEmpty()) {
        titleLine
    } else {
        taffyString(R.string.taffy_ask_page_spoken, titleLine, caption)
    }
    val removeDescription = taffyString(R.string.taffy_ask_remove_page, titleLine)
    val shape = TaffyTheme.shapes.pill
    val showRemove = onRemove != null
    Row(
        modifier = modifier
            .height(PageChipHeight)
            .clip(shape)
            .background(colors.surfaceRaised)
            .border(
                if (page.taffyOpened) TaffyBorders.rail else TaffyBorders.standard,
                if (page.taffyOpened) colors.accent else colors.outline,
                shape,
            )
            .padding(
                start = TaffyTheme.spacing.tight,
                end = if (showRemove) 0.dp else TaffyTheme.spacing.snug,
            )
            .testTag("$START_PAGE_CHIP_TEST_TAG_PREFIX${page.tabId.value}")
            .semantics(mergeDescendants = true) { contentDescription = spoken },
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(TaffyTheme.spacing.tight),
    ) {
        TaffyGlyphFrame(size = PageChipMark) {
            val initial = page.host.firstOrNull()?.uppercaseChar()?.toString()
                ?: page.title.firstOrNull()?.uppercaseChar()?.toString()
                ?: ""
            if (initial.isNotEmpty()) {
                Text(
                    text = initial,
                    style = TaffyTheme.typography.caption,
                    color = colors.textSecondary,
                )
            }
        }
        Text(
            text = titleLine,
            style = TaffyTheme.typography.detail,
            color = if (page.closed) colors.caution else colors.textPrimary,
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
            modifier = Modifier.widthIn(max = PageChipTitleMax),
        )
        if (page.closed) {
            Icon(
                imageVector = TaffyIcon.Warning,
                contentDescription = null,
                tint = colors.caution,
                modifier = Modifier.size(PageChipGlyph),
            )
        }
        if (onRemove != null) {
            Box(
                modifier = Modifier
                    .size(PageChipHeight)
                    .clickable(role = Role.Button, onClick = onRemove)
                    .semantics { contentDescription = removeDescription },
                contentAlignment = Alignment.Center,
            ) {
                Icon(
                    imageVector = TaffyIcon.X,
                    contentDescription = null,
                    tint = colors.textSecondary,
                    modifier = Modifier.size(PageChipGlyph),
                )
            }
        }
    }
}

/** One page chip under the box, by the tab it names. */
const val START_PAGE_CHIP_TEST_TAG_PREFIX: String = "start_page_chip_"

private val PageChipHeight = 32.dp
private val PageChipMark = 20.dp
private val PageChipGlyph = 14.dp
private val PageChipTitleMax = 168.dp

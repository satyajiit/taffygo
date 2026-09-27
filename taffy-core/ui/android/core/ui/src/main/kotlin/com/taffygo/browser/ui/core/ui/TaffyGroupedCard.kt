// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyListScope
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.unit.Dp
import com.taffygo.browser.ui.core.designsystem.TaffyBorders
import com.taffygo.browser.ui.core.designsystem.TaffyTheme

/**
 * One raised card whose children are rows, with hairline dividers inset past
 * the 40 dp glyph well.
 *
 * No outer gutter — [TaffyScreen] already applies one. Do not nest a
 * [TaffyListRow] here; that row draws its own border.
 */
@Composable
fun TaffyGroupedCard(
    modifier: Modifier = Modifier,
    testTag: String? = null,
    content: @Composable ColumnScope.() -> Unit,
) {
    Column(
        modifier = modifier
            .fillMaxWidth()
            .clip(TaffyTheme.shapes.card)
            .background(TaffyTheme.colors.surfaceRaised)
            .border(TaffyBorders.standard, TaffyTheme.colors.outline, TaffyTheme.shapes.card)
            .then(if (testTag != null) Modifier.testTag(testTag) else Modifier),
        content = content,
    )
}

/**
 * Virtualized raised-card rows for a collection whose size is not a small
 * compile-time constant.
 *
 * The lazy list owns scrolling and this helper owns the card skin. Keeping the
 * two at the same seam prevents a feature from putting one eager [Column]
 * inside a [androidx.compose.foundation.lazy.LazyColumn], which only moves the
 * performance problem behind a lazy-looking parent. Every [itemContent] body
 * is composed only when its own list item enters the lazy viewport.
 *
 * [key] is required because these rows normally carry durable identities and
 * must keep their remembered state when the collection changes. [testTag]
 * deliberately names each value rather than an index for the same reason.
 */
fun <T> LazyListScope.taffyGroupedCardItems(
    values: List<T>,
    key: (T) -> Any,
    contentType: (T) -> Any? = { null },
    testTag: (T) -> String? = { null },
    itemContent: @Composable ColumnScope.(T) -> Unit,
) {
    items(
        count = values.size,
        key = { index -> key(values[index]) },
        contentType = { index -> contentType(values[index]) },
    ) { index ->
        val value = values[index]
        TaffyGroupedCard(testTag = testTag(value)) {
            itemContent(value)
        }
    }
}

/**
 * A 1 dp hairline. Default inset clears the 40 dp glyph well plus its gap.
 * Pass `0.dp` for a full-bleed rule, matching OpenAlly settings rows.
 */
@Composable
fun TaffyGroupedCardDivider(
    modifier: Modifier = Modifier,
    startInset: Dp = Dp.Unspecified,
) {
    val inset = if (startInset == Dp.Unspecified) {
        TaffyTheme.spacing.screenMargin +
            TaffyGlyphFrameSize +
            TaffyTheme.spacing.snug
    } else {
        startInset
    }
    Box(
        modifier = modifier
            .padding(start = inset)
            .fillMaxWidth()
            .height(TaffyBorders.standard)
            .background(TaffyTheme.colors.outline),
    )
}

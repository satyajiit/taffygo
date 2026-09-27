// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.ui

import androidx.compose.foundation.lazy.grid.GridItemSpan
import androidx.compose.foundation.lazy.grid.LazyGridScope
import androidx.compose.foundation.lazy.grid.items
import androidx.compose.runtime.Composable

/**
 * One tile in a lazy bento body ([TaffyLazyGridScreen]).
 *
 * The span is clamped to the line, so a [TaffyBentoSpan.Full] item is the
 * whole row on any column count and a [TaffyBentoSpan.Two] item is the row on
 * a folded grid.
 */
fun LazyGridScope.taffyBentoItem(
    key: Any? = null,
    span: TaffyBentoSpan = TaffyBentoSpan.One,
    contentType: Any? = null,
    content: @Composable () -> Unit,
) {
    item(
        key = key,
        span = { GridItemSpan(minOf(span.cells, maxLineSpan)) },
        contentType = contentType,
    ) {
        content()
    }
}

/**
 * Keyed tiles for a collection whose size is not a small compile-time
 * constant: one per value, each with the span [span] gives it.
 */
fun <T> LazyGridScope.taffyBentoItems(
    values: List<T>,
    key: (T) -> Any,
    span: (T) -> TaffyBentoSpan = { TaffyBentoSpan.One },
    contentType: (T) -> Any? = { null },
    itemContent: @Composable (T) -> Unit,
) {
    items(
        items = values,
        key = key,
        span = { value -> GridItemSpan(minOf(span(value).cells, maxLineSpan)) },
        contentType = contentType,
    ) { value ->
        itemContent(value)
    }
}

// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.ui

import androidx.compose.runtime.Composable

/** What a non-lazy bento body may add: tiles, in reading order, each with a span. */
interface TaffyBentoScope {
    /** One tile. It is placed after the tiles before it, never around them. */
    fun item(span: TaffyBentoSpan = TaffyBentoSpan.One, content: @Composable () -> Unit)
}

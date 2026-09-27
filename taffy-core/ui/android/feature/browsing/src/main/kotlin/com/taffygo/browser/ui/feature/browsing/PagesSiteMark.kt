// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.browsing

import android.graphics.Bitmap
import androidx.compose.foundation.Image
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.layout.ContentScale
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.ui.TaffyGlyphFrame

/**
 * The site's own mark from the local store, or its initial on a sunken disc.
 *
 * Never a network fetch, and never the start-page ribbon: History and
 * Bookmarks are records, not tiles on a start grid.
 */
@Composable
internal fun PagesSiteMark(
    host: String,
    mark: Bitmap?,
    modifier: Modifier = Modifier,
) {
    TaffyGlyphFrame(modifier = modifier) {
        if (mark != null) {
            Image(
                bitmap = mark.asImageBitmap(),
                contentDescription = null,
                contentScale = ContentScale.Crop,
                modifier = Modifier.fillMaxSize(),
            )
        } else {
            Text(
                text = siteInitial(host),
                style = TaffyTheme.typography.title,
                color = TaffyTheme.colors.textSecondary,
            )
        }
    }
}

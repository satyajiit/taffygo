// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import android.content.Context
import android.graphics.Bitmap
import androidx.compose.foundation.Image
import androidx.compose.foundation.layout.size
import androidx.compose.material3.Icon
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.produceState
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.ui.LocalAppDispatchers
import com.taffygo.browser.ui.core.ui.TaffyGlyphFrame
import com.taffygo.browser.ui.core.ui.TaffyIcon
import kotlinx.coroutines.withContext

/** The vendor favicon for one engine, or a magnifying glass when it cannot be read. */
@Composable
fun SearchEngineMark(markFile: String, modifier: Modifier = Modifier) {
    val context = LocalContext.current.applicationContext
    val ioDispatcher = LocalAppDispatchers.current.io
    val bitmap by produceState<Bitmap?>(
        initialValue = null,
        key1 = markFile,
        key2 = context,
        key3 = ioDispatcher,
    ) {
        value = withContext(ioDispatcher) {
            loadSearchEngineMark(context, markFile)
        }
    }
    val loadedBitmap = bitmap
    TaffyGlyphFrame(modifier = modifier.size(MarkHost)) {
        if (loadedBitmap != null) {
            Image(
                bitmap = loadedBitmap.asImageBitmap(),
                contentDescription = null,
                modifier = Modifier.size(MarkSize),
            )
        } else {
            Icon(
                imageVector = TaffyIcon.MagnifyingGlass,
                contentDescription = null,
                tint = TaffyTheme.colors.textSecondary,
                modifier = Modifier.size(MarkSize),
            )
        }
    }
}

internal fun loadSearchEngineMark(context: Context, markFile: String): Bitmap? {
    val asset = runCatching {
        context.assets.open(markFile).use { stream ->
            val bytes = ByteArray(MAX_SEARCH_ENGINE_MARK_BYTES + 1)
            var length = 0
            while (length < bytes.size) {
                val read = stream.read(bytes, length, bytes.size - length)
                if (read <= 0) break
                length += read
            }
            if (length == 0 || length > MAX_SEARCH_ENGINE_MARK_BYTES) null else bytes.copyOf(length)
        }
    }.getOrNull()
        ?: return null
    return decodeSearchEngineMark(asset)
}

private val MarkHost = 40.dp
private val MarkSize = 24.dp

internal const val MAX_SEARCH_ENGINE_MARK_BYTES = 64 * 1024

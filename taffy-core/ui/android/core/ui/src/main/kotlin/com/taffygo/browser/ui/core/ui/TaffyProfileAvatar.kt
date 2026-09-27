// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.ui

import android.content.Context
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.size
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.produceState
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Shape
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.unit.Dp
import com.taffygo.browser.ui.core.designsystem.TaffyBorders
import com.taffygo.browser.ui.core.designsystem.TaffyTheme
import com.taffygo.browser.ui.core.model.LocalAvatar
import java.io.IOException
import kotlinx.coroutines.withContext

/**
 * The face of this phone's profile, at one size.
 *
 * Lives in `:core:ui` because two screens draw the same face and neither may
 * reach the other: first run (SCR-007) and the person hub (SCR-410) are in
 * different feature modules, and a profile that looked different depending on
 * which screen you were on would not be one profile.
 *
 * Every selectable picture is committed in this module's own assets. That is
 * the point rather than a convenience: opening a local identity surface must
 * not disclose a device to an asset host, and the first paint must not depend
 * on a network. Nothing here takes a URL, and `LocalAvatar` persists an id.
 */
@Composable
fun TaffyProfileAvatar(
    avatar: LocalAvatar,
    monogram: String,
    size: Dp,
    modifier: Modifier = Modifier,
    selected: Boolean = false,
    shape: Shape = TaffyTheme.shapes.card,
) {
    Box(
        modifier = modifier
            .size(size)
            .clip(shape)
            .background(TaffyTheme.colors.accentWash)
            .then(
                if (selected) {
                    Modifier.border(TaffyBorders.emphasis, TaffyTheme.colors.accent, shape)
                } else {
                    Modifier
                },
            ),
        contentAlignment = Alignment.Center,
    ) {
        when (avatar) {
            LocalAvatar.Monogram -> TaffyMonogram(
                monogram = monogram,
                modifier = Modifier.size(size),
            )
            is LocalAvatar.Tile -> TaffyProfileTile(id = avatar.id, size = size)
        }
    }
}

/**
 * One committed picture, decoded off the UI thread.
 *
 * A tile whose bytes cannot be read draws nothing rather than falling back to
 * another picture. The fallback that used to sit here dated from when an id
 * could name a file that was never bundled; ids are now validated in
 * `:core:model` against the shipped set, so a read failure is a real defect
 * and quietly substituting a different face would hide it.
 */
@Composable
private fun TaffyProfileTile(id: String, size: Dp) {
    val context = LocalContext.current.applicationContext
    val ioDispatcher = LocalAppDispatchers.current.io
    val targetPixels = with(LocalDensity.current) { size.roundToPx().coerceAtLeast(1) }
    val bitmap by produceState<Bitmap?>(
        initialValue = null,
        key1 = context,
        key2 = id,
        key3 = ioDispatcher to targetPixels,
    ) {
        value = withContext(ioDispatcher) { decodeTile(context, id, targetPixels) }
    }
    val picture = bitmap
    if (picture != null) {
        Image(
            bitmap = picture.asImageBitmap(),
            contentDescription = null,
            contentScale = ContentScale.Crop,
            modifier = Modifier.size(size),
        )
    }
}

/** Where a tile's bytes live inside this module's assets. */
private const val TILE_ASSET_DIR = "profile-banners"

private fun decodeTile(context: Context, id: String, targetPixels: Int): Bitmap? = try {
    val path = "$TILE_ASSET_DIR/$id.webp"
    val bounds = BitmapFactory.Options().apply { inJustDecodeBounds = true }
    context.assets.open(path).use { stream ->
        BitmapFactory.decodeStream(stream, null, bounds)
    }
    if (bounds.outWidth <= 0 || bounds.outHeight <= 0) {
        null
    } else {
        val options = BitmapFactory.Options().apply {
            inSampleSize = taffyTileSampleSize(bounds.outWidth, bounds.outHeight, targetPixels)
        }
        context.assets.open(path).use { stream ->
            BitmapFactory.decodeStream(stream, null, options)
        }
    }
} catch (_: IOException) {
    null
}

/** Largest power-of-two decode sample that still covers the requested square. */
internal fun taffyTileSampleSize(width: Int, height: Int, targetPixels: Int): Int {
    if (width <= 0 || height <= 0 || targetPixels <= 0) return 1
    var sample = 1
    while (sample <= Int.MAX_VALUE / 2) {
        val next = sample * 2
        if (width / next < targetPixels || height / next < targetPixels) break
        sample = next
    }
    return sample
}

// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.app

import android.graphics.BitmapFactory
import androidx.compose.ui.graphics.ImageBitmap
import androidx.compose.ui.graphics.asImageBitmap

/**
 * Decodes one WebP member of an installed delivery pack.
 *
 * The caller states the size it published, and a body of any other size is no
 * artwork rather than a surprise. The bounds-only pass happens first and on
 * purpose: without it a malformed compressed member turns the Core API's
 * general four-megabyte member allowance into a bitmap of whatever size the
 * header claims. [maxEncodedBytes] is the second half of that guard, applied
 * before anything is handed to the decoder at all.
 *
 * [targetWidthPx] is what the surface will actually draw, not what the pack
 * holds. A plate published at 640 by 480 and drawn at 120 dp does not need
 * every pixel, and `inSampleSize` is the only place that can be said cheaply —
 * after the decode the memory is already spent.
 */
object PackWebp {
    fun decode(
        bytes: ByteArray,
        expectedWidth: Int,
        expectedHeight: Int,
        maxEncodedBytes: Int,
        targetWidthPx: Int = expectedWidth,
    ): ImageBitmap? {
        if (bytes.isEmpty() || bytes.size > maxEncodedBytes) {
            return null
        }
        val bounds = BitmapFactory.Options().apply { inJustDecodeBounds = true }
        BitmapFactory.decodeByteArray(bytes, 0, bytes.size, bounds)
        if (bounds.outWidth != expectedWidth || bounds.outHeight != expectedHeight) {
            return null
        }
        val options = BitmapFactory.Options().apply {
            inSampleSize = sampleSize(expectedWidth, targetWidthPx)
        }
        val bitmap = BitmapFactory.decodeByteArray(bytes, 0, bytes.size, options) ?: return null
        // Checked again after the decode, against what the sample asked for: a
        // body whose header and payload disagree is the case the pre-pass
        // cannot see.
        val sample = options.inSampleSize.coerceAtLeast(1)
        if (bitmap.width != expectedWidth / sample || bitmap.height != expectedHeight / sample) {
            bitmap.recycle()
            return null
        }
        return bitmap.asImageBitmap()
    }

    /** Largest power-of-two decode sample that still covers [targetWidthPx]. */
    internal fun sampleSize(width: Int, targetWidthPx: Int): Int {
        if (width <= 0 || targetWidthPx <= 0) return 1
        var sample = 1
        while (sample <= Int.MAX_VALUE / 2) {
            val next = sample * 2
            if (width / next < targetWidthPx) break
            sample = next
        }
        return sample
    }
}

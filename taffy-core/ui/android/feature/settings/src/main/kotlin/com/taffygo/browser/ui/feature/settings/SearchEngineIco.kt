// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import android.graphics.Bitmap
import android.graphics.BitmapFactory

/**
 * Decode a vendor favicon. Android's bitmap factory reads PNG payloads; many
 * `.ico` files are a directory plus one PNG, so a failed whole-file decode
 * tries the largest image inside the directory.
 */
internal fun decodeSearchEngineMark(bytes: ByteArray): Bitmap? {
    if (!searchEngineMarkInputAllowed(bytes.size)) return null
    decodeBoundedImage(bytes, 0, bytes.size)?.let { return it }
    val payload = largestIcoPayload(bytes) ?: return null
    return decodeBoundedImage(bytes, payload.first, payload.last - payload.first + 1)
}

internal fun searchEngineMarkInputAllowed(size: Int): Boolean =
    size in 1..MAX_SEARCH_ENGINE_MARK_BYTES

internal fun largestIcoPayload(bytes: ByteArray): IntRange? {
    if (bytes.size < ICONDIR) return null
    if (u16(bytes, 0) != 0 || u16(bytes, 2) != ICO_TYPE) return null
    val count = u16(bytes, 4)
    if (count !in 1..MAX_ICON_ENTRIES || ICONDIR + count * ICONDIRENTRY > bytes.size) return null
    var bestOffset = 0
    var bestSize = 0
    var bestArea = 0
    repeat(count) { index ->
        val entry = ICONDIR + index * ICONDIRENTRY
        val size = u32(bytes, entry + 8)
        val offset = u32(bytes, entry + 12)
        val width = icoDimension(bytes[entry].toInt() and 0xFF)
        val height = icoDimension(bytes[entry + 1].toInt() and 0xFF)
        val area = width * height
        if (
            size in 1..bytes.size.toLong() &&
            offset in 0..bytes.size.toLong() &&
            size <= bytes.size.toLong() - offset &&
            width <= MAX_SEARCH_ENGINE_MARK_SOURCE_DIMENSION &&
            height <= MAX_SEARCH_ENGINE_MARK_SOURCE_DIMENSION &&
            (area > bestArea || (area == bestArea && size > bestSize.toLong()))
        ) {
            bestSize = size.toInt()
            bestOffset = offset.toInt()
            bestArea = area
        }
    }
    if (bestSize == 0) return null
    return bestOffset until bestOffset + bestSize
}

private fun decodeBoundedImage(bytes: ByteArray, offset: Int, length: Int): Bitmap? {
    val bounds = BitmapFactory.Options().apply { inJustDecodeBounds = true }
    BitmapFactory.decodeByteArray(bytes, offset, length, bounds)
    if (!searchEngineMarkDimensionsAllowed(bounds.outWidth, bounds.outHeight)) return null
    val options = BitmapFactory.Options().apply {
        inSampleSize = searchEngineMarkSampleSize(bounds.outWidth, bounds.outHeight)
    }
    return BitmapFactory.decodeByteArray(bytes, offset, length, options)?.also { bitmap ->
        if (
            bitmap.width !in 1..MAX_SEARCH_ENGINE_MARK_DECODED_DIMENSION ||
            bitmap.height !in 1..MAX_SEARCH_ENGINE_MARK_DECODED_DIMENSION
        ) {
            bitmap.recycle()
            return null
        }
    }
}

private fun u16(bytes: ByteArray, at: Int): Int =
    (bytes[at].toInt() and 0xFF) or ((bytes[at + 1].toInt() and 0xFF) shl 8)

private fun u32(bytes: ByteArray, at: Int): Long =
    (bytes[at].toLong() and 0xFF) or
        ((bytes[at + 1].toLong() and 0xFF) shl 8) or
        ((bytes[at + 2].toLong() and 0xFF) shl 16) or
        ((bytes[at + 3].toLong() and 0xFF) shl 24)

private fun icoDimension(encoded: Int): Int = if (encoded == 0) 256 else encoded

internal fun searchEngineMarkDimensionsAllowed(width: Int, height: Int): Boolean =
    width in 1..MAX_SEARCH_ENGINE_MARK_SOURCE_DIMENSION &&
        height in 1..MAX_SEARCH_ENGINE_MARK_SOURCE_DIMENSION

internal fun searchEngineMarkSampleSize(width: Int, height: Int): Int {
    var sample = 1
    while (
        width / sample > MAX_SEARCH_ENGINE_MARK_DECODED_DIMENSION ||
        height / sample > MAX_SEARCH_ENGINE_MARK_DECODED_DIMENSION
    ) {
        sample *= 2
    }
    return sample
}

private const val ICONDIR = 6
private const val ICONDIRENTRY = 16
private const val ICO_TYPE = 1
private const val MAX_ICON_ENTRIES = 256
private const val MAX_SEARCH_ENGINE_MARK_SOURCE_DIMENSION = 512
private const val MAX_SEARCH_ENGINE_MARK_DECODED_DIMENSION = 192

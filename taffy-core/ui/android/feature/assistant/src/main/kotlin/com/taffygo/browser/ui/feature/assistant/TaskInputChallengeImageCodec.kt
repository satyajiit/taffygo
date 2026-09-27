// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.assistant

import android.graphics.BitmapFactory
import androidx.compose.ui.graphics.ImageBitmap
import androidx.compose.ui.graphics.asImageBitmap

/** Decode only the bounded PNG shape the browser's challenge capture emits. */
internal fun decodeTaskInputChallengeImage(bytes: ByteArray): ImageBitmap? {
    if (!taskInputChallengeImageInputAllowed(bytes)) return null
    val bounds = BitmapFactory.Options().apply { inJustDecodeBounds = true }
    BitmapFactory.decodeByteArray(bytes, 0, bytes.size, bounds)
    if (!taskInputChallengeImageDimensionsAllowed(bounds.outWidth, bounds.outHeight)) return null
    val bitmap = BitmapFactory.decodeByteArray(bytes, 0, bytes.size) ?: return null
    if (!taskInputChallengeImageDimensionsAllowed(bitmap.width, bitmap.height)) {
        bitmap.recycle()
        return null
    }
    return bitmap.asImageBitmap()
}

internal fun taskInputChallengeImageInputAllowed(bytes: ByteArray): Boolean =
    bytes.size in 1..MAX_TASK_INPUT_CHALLENGE_IMAGE_BYTES &&
        bytes.size >= PNG_SIGNATURE.size &&
        PNG_SIGNATURE.indices.all { index -> bytes[index] == PNG_SIGNATURE[index] }

internal fun taskInputChallengeImageDimensionsAllowed(width: Int, height: Int): Boolean =
    width in 1..MAX_TASK_INPUT_CHALLENGE_IMAGE_DIMENSION &&
        height in 1..MAX_TASK_INPUT_CHALLENGE_IMAGE_DIMENSION

internal const val MAX_TASK_INPUT_CHALLENGE_IMAGE_BYTES = 512 * 1024
internal const val MAX_TASK_INPUT_CHALLENGE_IMAGE_DIMENSION = 384

private val PNG_SIGNATURE = byteArrayOf(
    0x89.toByte(),
    0x50,
    0x4E,
    0x47,
    0x0D,
    0x0A,
    0x1A,
    0x0A,
)

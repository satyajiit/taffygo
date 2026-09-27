// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.assistant

import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class TaskInputChallengeImageCodecTest {
    @Test
    fun `only bounded PNG input reaches the bitmap decoder`() {
        assertFalse(taskInputChallengeImageInputAllowed(byteArrayOf()))
        assertFalse(taskInputChallengeImageInputAllowed(ByteArray(8)))
        assertTrue(taskInputChallengeImageInputAllowed(png(MAX_TASK_INPUT_CHALLENGE_IMAGE_BYTES)))
        assertFalse(
            taskInputChallengeImageInputAllowed(
                png(MAX_TASK_INPUT_CHALLENGE_IMAGE_BYTES + 1),
            ),
        )
    }

    @Test
    fun `decoded dimensions cannot create an oversized bitmap`() {
        assertTrue(taskInputChallengeImageDimensionsAllowed(1, 1))
        assertTrue(
            taskInputChallengeImageDimensionsAllowed(
                MAX_TASK_INPUT_CHALLENGE_IMAGE_DIMENSION,
                MAX_TASK_INPUT_CHALLENGE_IMAGE_DIMENSION,
            ),
        )
        assertFalse(
            taskInputChallengeImageDimensionsAllowed(
                MAX_TASK_INPUT_CHALLENGE_IMAGE_DIMENSION + 1,
                1,
            ),
        )
        assertFalse(taskInputChallengeImageDimensionsAllowed(0, 1))
    }

    private fun png(size: Int): ByteArray = ByteArray(size).also { bytes ->
        val signature = byteArrayOf(
            0x89.toByte(),
            0x50,
            0x4E,
            0x47,
            0x0D,
            0x0A,
            0x1A,
            0x0A,
        )
        signature.copyInto(bytes)
    }
}

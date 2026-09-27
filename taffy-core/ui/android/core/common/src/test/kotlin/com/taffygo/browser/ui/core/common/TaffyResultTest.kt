// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.common

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

/**
 * The total result type.
 *
 * A boundary returns one of these rather than throwing, so a caller cannot
 * forget the failure path and a screen never parses a message to find out what
 * happened.
 */
class TaffyResultTest {

    @Test
    fun `a success carries its value and no reason`() {
        val result: TaffyResult<String> = TaffyResult.Success("snapshot")

        assertEquals("snapshot", result.valueOrNull())
        assertNull(result.reasonOrNull())
    }

    @Test
    fun `a failure carries its reason and no value`() {
        val result: TaffyResult<String> = TaffyResult.Failure(FailureReason.NOT_FOUND)

        assertNull(result.valueOrNull())
        assertEquals(FailureReason.NOT_FOUND, result.reasonOrNull())
    }

    @Test
    fun `the failure vocabulary is closed and every reason is distinct`() {
        val names = FailureReason.entries.map { it.name }

        assertEquals(names.size, names.toSet().size)
    }

    @Test
    fun `a caller that handles both cases handles every case`() {
        val results: List<TaffyResult<Int>> = listOf(
            TaffyResult.Success(1),
            TaffyResult.Failure(FailureReason.MALFORMED),
        )

        val handled = results.map { result ->
            when (result) {
                is TaffyResult.Success -> result.value
                is TaffyResult.Failure -> -1
            }
        }

        assertEquals(listOf(1, -1), handled)
    }
}

// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class ClearBrowsingDataReducerTest {

    @Test
    fun `selecting a range keeps the classes`() {
        val before = ClearBrowsingDataUiState(available = true)
        val after = reduceClearBrowsingData(
            before,
            ClearBrowsingDataIntent.SelectRange(ClearBrowsingDataUiState.Range.ALL_TIME),
        )
        assertEquals(ClearBrowsingDataUiState.Range.ALL_TIME, after.range)
        assertEquals(before.classes, after.classes)
    }

    @Test
    fun `toggling a class off does not force it back on`() {
        val after = reduceClearBrowsingData(
            ClearBrowsingDataUiState(available = true),
            ClearBrowsingDataIntent.ToggleClass(ClearBrowsingDataUiState.DataClass.HISTORY),
        )
        assertFalse(ClearBrowsingDataUiState.DataClass.HISTORY in after.classes)
        assertTrue(ClearBrowsingDataUiState.DataClass.COOKIES in after.classes)
    }

    @Test
    fun `confirm is refused when clearing is unavailable`() {
        val state = ClearBrowsingDataUiState(available = false)
        assertFalse(state.canClear)
        assertEquals(state, reduceClearBrowsingData(state, ClearBrowsingDataIntent.Confirm))
    }

    @Test
    fun `confirm opens the sheet when clearing is available`() {
        val after = reduceClearBrowsingData(
            ClearBrowsingDataUiState(available = true),
            ClearBrowsingDataIntent.Confirm,
        )
        assertTrue(after.confirming)
    }

    @Test
    fun `workspaces are never a data class`() {
        val names = ClearBrowsingDataUiState.DataClass.entries.map { it.name }
        assertFalse(names.any { it.contains("WORKSPACE", ignoreCase = true) })
        assertFalse(names.any { it.contains("LIBRARY", ignoreCase = true) })
        assertFalse(names.any { it.contains("MEMORY", ignoreCase = true) })
    }

    @Test
    fun `a class without a backing store cannot be selected`() {
        val supported = setOf(ClearBrowsingDataUiState.DataClass.HISTORY)
        val state = ClearBrowsingDataUiState(
            available = true,
            classes = supported,
            supportedClasses = supported,
        )
        assertEquals(
            state,
            reduceClearBrowsingData(
                state,
                ClearBrowsingDataIntent.ToggleClass(
                    ClearBrowsingDataUiState.DataClass.TIME_ON_SITES,
                ),
            ),
        )
    }

    @Test
    fun `a new choice clears an earlier failure without claiming success`() {
        val state = ClearBrowsingDataUiState(available = true, failed = true)

        val changed = reduceClearBrowsingData(
            state,
            ClearBrowsingDataIntent.SelectRange(ClearBrowsingDataUiState.Range.LAST_DAY),
        )

        assertFalse(changed.failed)
        assertEquals(ClearBrowsingDataUiState.Range.LAST_DAY, changed.range)
    }
}

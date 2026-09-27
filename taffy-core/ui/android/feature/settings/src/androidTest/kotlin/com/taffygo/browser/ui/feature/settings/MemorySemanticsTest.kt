// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.performClick
import androidx.compose.ui.test.performScrollToIndex
import com.taffygo.browser.ui.core.ui.EMPTY_STATE_TEST_TAG
import com.taffygo.browser.ui.core.ui.TaffyDestination
import com.taffygo.browser.ui.core.ui.TaffyPreview
import org.junit.Assert.assertEquals
import org.junit.Rule
import org.junit.Test

/** Screen SCR-505 — promise, groups, search only above eight. */
class MemorySemanticsTest {

    @get:Rule
    val compose = createComposeRule()

    private val intents = mutableListOf<MemoryIntent>()

    @Test
    fun emptyShowsThePromiseAndAdd() {
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                MemoryContent(
                    state = MemoryUiState(processScoped = true),
                    onIntent = { intents += it },
                )
            }
        }

        compose.onNodeWithTag(TaffyDestination.Memory.screenId).assertExists()
        compose.onNodeWithTag(MEMORY_PROMISE_TEST_TAG).assertExists()
        compose.onNodeWithTag(EMPTY_STATE_TEST_TAG).assertExists()
        compose.onNodeWithTag(MEMORY_SEARCH_TEST_TAG).assertDoesNotExist()
        compose.onNodeWithTag(MEMORY_EMPTY_ADD_TEST_TAG).performClick()
        assertEquals(listOf(MemoryIntent.Add), intents)
    }

    @Test
    fun searchAppearsOnlyAboveEightRows() {
        val notes = List(9) { index ->
            MemoryRepository.Note(
                id = "n$index",
                statement = "Note $index",
                source = MemoryRepository.Source.YOU_WROTE,
                addedEpochDay = 20_300,
            )
        }
        compose.setContent {
            TaffyPreview(darkTheme = true) {
                MemoryContent(
                    state = memoryState(notes, processScoped = true),
                    onIntent = { intents += it },
                )
            }
        }

        compose.onNodeWithTag(MEMORY_SEARCH_TEST_TAG).assertExists()
        compose.onNodeWithTag("${MEMORY_ROW_TEST_TAG_PREFIX}n0").assertExists()
    }

    @Test
    fun aLargeMemoryComposesOnlyVisibleRows() {
        val notes = List(512) { index ->
            MemoryRepository.Note(
                id = "n$index",
                statement = "Note $index",
                source = MemoryRepository.Source.YOU_WROTE,
                addedEpochDay = 20_300,
            )
        }
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                MemoryContent(state = memoryState(notes), onIntent = {})
            }
        }

        compose.onNodeWithTag("${MEMORY_ROW_TEST_TAG_PREFIX}n511").assertDoesNotExist()
        compose.onNodeWithTag(MEMORY_LIST_TEST_TAG).performScrollToIndex(notes.size)
        compose.onNodeWithTag("${MEMORY_ROW_TEST_TAG_PREFIX}n511").assertExists()
        compose.onNodeWithTag("${MEMORY_ROW_TEST_TAG_PREFIX}n0").assertDoesNotExist()
    }

    private fun memoryState(
        notes: List<MemoryRepository.Note>,
        processScoped: Boolean = false,
    ) = projectMemory(
        snapshot = MemoryRepository.Snapshot(
            availability = YouSurfaceAvailability.READY,
            notes = notes,
            processScoped = processScoped,
        ),
        query = "",
        editor = null,
        confirmDelete = false,
    )
}

// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.feature.settings

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertSame
import org.junit.Assert.assertTrue
import org.junit.Test

/** Screen SCR-505: why-line required, search only above eight, no hidden store. */
class MemoryReducerTest {

    private val ready = projectMemory(
        MemoryRepository.Snapshot(
            availability = YouSurfaceAvailability.READY,
            notes = YouFixtures.memoryNotes,
            processScoped = true,
            revision = 7uL,
        ),
        query = "",
        editor = null,
        confirmDelete = false,
    )

    @Test
    fun `search chrome is off at eight notes or fewer`() {
        assertFalse(ready.showSearch)
        val notes = YouFixtures.memoryNotes + List(7) { index ->
            MemoryRepository.Note(
                id = "n$index",
                statement = "Note $index",
                source = MemoryRepository.Source.YOU_WROTE,
                addedEpochDay = 20_300,
            )
        }
        val nine = projectMemory(
            MemoryRepository.Snapshot(YouSurfaceAvailability.READY, notes = notes),
            query = "",
            editor = null,
            confirmDelete = false,
        )
        assertEquals(9, nine.totalNoteCount)
        assertTrue(nine.showSearch)
    }

    @Test
    fun `groups split you wrote and Taffy noticed`() {
        val visible = ready.visibleNotes

        assertEquals(listOf("m1"), visible.youWrote.map { it.id })
        assertEquals(listOf("m2"), visible.taffyNoticed.map { it.id })
        assertEquals(
            listOf("m2"),
            projectMemory(
                MemoryRepository.Snapshot(
                    availability = YouSurfaceAvailability.READY,
                    notes = YouFixtures.memoryNotes,
                    search = MemoryRepository.Search("BULLET", setOf("m2")),
                ),
                query = "BULLET",
                editor = null,
                confirmDelete = false,
            ).visibleNotes.taffyNoticed.map { it.id },
        )
    }

    @Test
    fun `visible note projection traverses the store once`() {
        val notes = CountingList(
            List(40) { index ->
                MemoryRepository.Note(
                    id = "n$index",
                    statement = if (index % 2 == 0) "Matching note $index" else "Other note $index",
                    source = if (index % 3 == 0) {
                        MemoryRepository.Source.YOU_WROTE
                    } else {
                        MemoryRepository.Source.TAFFY_NOTICED
                    },
                    addedEpochDay = 20_300,
                )
            },
        )

        val state = projectMemory(
            MemoryRepository.Snapshot(YouSurfaceAvailability.READY, notes = notes),
            query = "matching",
            editor = null,
            confirmDelete = false,
        )
        val visible = state.visibleNotes

        repeat(8) {
            assertSame(visible, state.visibleNotes)
        }

        assertEquals(notes.size, notes.elementReads)
    }

    @Test
    fun `unavailable add is refused and no fake rows`() {
        val unavailable = projectMemory(
            MemoryRepository.Snapshot(availability = YouSurfaceAvailability.UNAVAILABLE),
            query = "",
            editor = null,
            confirmDelete = false,
        )
        assertTrue(unavailable.visibleNotes.isEmpty)
        assertEquals(unavailable, reduceMemory(unavailable, MemoryIntent.Add))
    }

    @Test
    fun `add and edit keep the why-line on an existing note`() {
        val adding = reduceMemory(ready, MemoryIntent.Add)
        assertNull(adding.editor?.id)
        val opened = reduceMemory(ready, MemoryIntent.Open("m2"))
        assertEquals("Answers in short bullet points", opened.editor?.text)
        assertEquals(MemoryRepository.Source.TAFFY_NOTICED, opened.editor?.whyNote?.source)
        val typed = reduceMemory(opened, MemoryIntent.ChangeText("Shorter answers"))
        assertEquals("Shorter answers", typed.editor?.text)
        assertEquals("m2", typed.editor?.whyNote?.id)
    }

    @Test
    fun `delete is confirmed`() {
        val opened = reduceMemory(ready, MemoryIntent.Open("m1"))
        val asking = reduceMemory(opened, MemoryIntent.Delete)
        assertTrue(asking.confirmDelete)
        val kept = reduceMemory(asking, MemoryIntent.CancelDelete)
        assertFalse(kept.confirmDelete)
        assertEquals("m1", kept.editor?.id)
    }

    private class CountingList<T>(
        private val values: List<T>,
    ) : AbstractList<T>() {
        var elementReads: Int = 0
            private set

        override val size: Int
            get() = values.size

        override fun get(index: Int): T {
            elementReads += 1
            return values[index]
        }
    }
}

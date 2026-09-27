// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.model

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * The mapping from the thirteen durable states to the seven displayed ones.
 *
 * It is a total function, so this test walks every durable state rather than
 * sampling. Two sentences the product depends on are asserted directly: partial
 * work has its own word, and stopped is never the same word as failed.
 */
class TaskStateDisplayTest {

    @Test
    fun `every durable state has an answer`() {
        assertEquals(13, TaskDurableState.entries.size)
        TaskDurableState.entries.forEach { it.display() }
    }

    @Test
    fun `setup and consent are surfaces rather than task states`() {
        assertNull(TaskDurableState.DRAFT.display())
        assertNull(TaskDurableState.AWAITING_CONSENT.display())
    }

    @Test
    fun `every displayed state is reachable from some durable one`() {
        val reached = TaskDurableState.entries.mapNotNull { it.display() }.toSet()

        assertEquals(TaskDisplayState.entries.toSet(), reached)
        assertEquals(7, TaskDisplayState.entries.size)
    }

    @Test
    fun `partial work keeps its own word and is not rounded up to done`() {
        assertEquals(TaskDisplayState.PARTLY_DONE, TaskDurableState.PARTIAL.display())
        assertEquals(TaskDisplayState.DONE, TaskDurableState.COMPLETED.display())
    }

    @Test
    fun `stopping is what the user did and is never conflated with failing`() {
        assertEquals(TaskDisplayState.STOPPED, TaskDurableState.CANCELLED.display())
        assertEquals(TaskDisplayState.STOPPED, TaskDurableState.CANCELLING.display())
        assertEquals(TaskDisplayState.FAILED, TaskDurableState.FAILED.display())
    }

    @Test
    fun `settling states still read as the state they are settling towards`() {
        assertTrue(TaskDurableState.PAUSING.isSettling)
        assertTrue(TaskDurableState.CANCELLING.isSettling)
        assertEquals(TaskDisplayState.PAUSED, TaskDurableState.PAUSING.display())
    }

    @Test
    fun `the four terminal states are terminal and no others are`() {
        val terminal = TaskDurableState.entries.filter { it.isTerminal }.toSet()

        assertEquals(TaskDurableState.TERMINAL, terminal)
        assertEquals(4, terminal.size)
    }

    @Test
    fun `every label is unique, because a label reaches an audit record`() {
        val labels = TaskDurableState.entries.map { it.label }

        assertEquals(labels.size, labels.toSet().size)
    }
}

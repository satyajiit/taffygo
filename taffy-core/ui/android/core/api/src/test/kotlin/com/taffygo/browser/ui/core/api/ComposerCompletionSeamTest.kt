// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.api

import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.flow.toList
import kotlinx.coroutines.launch
import kotlinx.coroutines.test.UnconfinedTestDispatcher
import kotlinx.coroutines.test.runTest
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * What the composer seam has to carry for a surface to be able to obey decision
 * `docs/decisions/0097-a-composer-suggestion-is-spent-from-the-persons-own-key.md`:
 * an identity on every answer, and a difference between no suggestion and a
 * suggestion of nothing.
 *
 * The opt-in is for [UnconfinedTestDispatcher], which these tests need rather
 * than prefer: a push channel drops what it emits at nobody, so a collector
 * that has not subscribed yet turns every assertion below into a tautology.
 */
@OptIn(ExperimentalCoroutinesApi::class)
class ComposerCompletionSeamTest {
    @Test
    fun `every answer names the request it answers`() = runTest {
        val client = RecordingCoreApiClient()
        val received = mutableListOf<ComposerCompletionReport>()
        // Unconfined so the collector is subscribed before the first push. A
        // report pushed at nobody is dropped, which is the channel behaving
        // correctly and would make this test pass for the wrong reason.
        backgroundScope.launch(UnconfinedTestDispatcher(testScheduler)) {
            client.composerCompletion.toList(received)
        }

        client.requestComposerCompletion(requestId = "typing-1", prefix = "the qu")
        client.requestComposerCompletion(requestId = "typing-2", prefix = "the quick br")
        client.deliverComposerCompletion(ComposerCompletionReport("typing-1", "ery is slow"))
        client.deliverComposerCompletion(ComposerCompletionReport("typing-2", "own fox"))

        assertEquals(listOf("typing-1", "typing-2"), received.map { it.requestId })
    }

    @Test
    fun `an answer to a superseded request is told apart by its identity`() = runTest {
        val client = RecordingCoreApiClient()
        val received = mutableListOf<ComposerCompletionReport>()
        backgroundScope.launch(UnconfinedTestDispatcher(testScheduler)) {
            client.composerCompletion.toList(received)
        }

        // The person kept typing, so the surface has moved on to typing-2. The
        // answer to typing-1 still arrives — a request in flight is not
        // unasked — and the surface has to be able to recognise it as stale.
        val current = "typing-2"
        client.deliverComposerCompletion(ComposerCompletionReport("typing-1", "stale"))
        client.deliverComposerCompletion(ComposerCompletionReport("typing-2", "fresh"))

        val superseded = received.filterNot { it.requestId == current }
        val shown = received.filter { it.requestId == current }
        assertEquals(listOf("stale"), superseded.map { it.text })
        assertEquals(listOf("fresh"), shown.map { it.text })
    }

    @Test
    fun `no suggestion is not a suggestion of nothing`() {
        val nothingOffered = ComposerCompletionReport("typing-1", text = null)
        val emptyOffered = ComposerCompletionReport("typing-1", text = "")

        // Same request, same shape, different fact. A surface that folded these
        // together would draw an empty ghost over the cursor for the first.
        assertNotEquals(nothingOffered, emptyOffered)
        assertNull(nothingOffered.text)
        assertEquals("", emptyOffered.text)
        assertTrue(emptyOffered.text?.isEmpty() == true)
    }

    @Test
    fun `absent text survives the push as absence`() = runTest {
        val client = RecordingCoreApiClient()
        val received = mutableListOf<ComposerCompletionReport>()
        backgroundScope.launch(UnconfinedTestDispatcher(testScheduler)) {
            client.composerCompletion.toList(received)
        }

        client.deliverComposerCompletion(ComposerCompletionReport("typing-1", null))
        client.deliverComposerCompletion(ComposerCompletionReport("typing-2", ""))

        assertEquals(listOf(null, ""), received.map { it.text })
    }

    @Test
    fun `a request states its prefix, and an absent suffix stays absent`() = runTest {
        val client = RecordingCoreApiClient()

        client.requestComposerCompletion(requestId = "typing-1", prefix = "dear ")
        client.requestComposerCompletion(
            requestId = "typing-2",
            prefix = "dear ",
            suffix = ", thank you",
        )

        assertEquals(
            listOf(
                RecordingCoreApiClient.ComposerRequest("typing-1", "dear ", null),
                RecordingCoreApiClient.ComposerRequest("typing-2", "dear ", ", thank you"),
            ),
            client.composerRequests,
        )
    }

    /**
     * A withdrawal names the request and asks for nothing (decision 0097
     * section 3). It is the half superseding does not cover: a composer that
     * stopped wanting an answer without asking for another one.
     */
    @Test
    fun `a withdrawal names one request and asks for none`() = runTest {
        val client = RecordingCoreApiClient()

        client.requestComposerCompletion(requestId = "typing-1", prefix = "dear ")
        client.cancelComposerCompletion("typing-1")

        assertEquals(listOf("typing-1"), client.withdrawnComposerRequests)
        assertEquals(listOf("typing-1"), client.composerRequests.map { it.requestId })
    }
}

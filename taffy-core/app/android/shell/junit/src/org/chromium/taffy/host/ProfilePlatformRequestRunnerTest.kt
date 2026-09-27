// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.host

import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.awaitCancellation
import kotlinx.coroutines.cancelAndJoin
import kotlinx.coroutines.launch
import kotlinx.coroutines.runBlocking
import org.chromium.base.test.BaseRobolectricTestRunner
import org.chromium.taffy.core_service.mojom.EffectStatus
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(BaseRobolectricTestRunner::class)
class ProfilePlatformRequestRunnerTest {
    @Test
    fun closeCancelsInFlightAdapterWorkButNotTheProfileScope() = runBlocking {
        val runner = ProfilePlatformRequestRunner(this)
        val started = CompletableDeferred<Unit>()
        val cancelled = CompletableDeferred<Unit>()
        val keepSiblingAlive = CompletableDeferred<Unit>()
        val sibling = launch { keepSiblingAlive.await() }

        runner.launch(
            block = {
                started.complete(Unit)
                try {
                    awaitCancellation()
                } finally {
                    cancelled.complete(Unit)
                }
            },
            failure = { error("A disconnected request must not answer its closed pipe") },
        )

        started.await()
        runner.close()
        cancelled.await()
        assertTrue(sibling.isActive)
        sibling.cancelAndJoin()
    }

    @Test
    fun closedRunnerRefusesNewWorkSynchronously() = runBlocking {
        val runner = ProfilePlatformRequestRunner(this)
        var status: Int? = null
        var ran = false
        runner.close()

        runner.launch(block = { ran = true }, failure = { status = it })

        assertEquals(EffectStatus.UNAVAILABLE, status)
        assertEquals(false, ran)
    }

    @Test
    fun teardownRunsEveryLegAndKeepsEveryFailure() {
        val ran = mutableListOf<String>()

        val failure = runCatching {
            runAllTeardownOperations(
                listOf(
                    { ran += "first" },
                    {
                        ran += "broken"
                        throw IllegalStateException("first failure")
                    },
                    {
                        ran += "also-broken"
                        throw IllegalArgumentException("second failure")
                    },
                    { ran += "last" },
                ),
            )
        }.exceptionOrNull()

        assertEquals(listOf("first", "broken", "also-broken", "last"), ran)
        assertTrue(failure is IllegalStateException)
        assertEquals("first failure", failure?.message)
        assertEquals(listOf("second failure"), failure?.suppressed?.map { it.message })
    }
}

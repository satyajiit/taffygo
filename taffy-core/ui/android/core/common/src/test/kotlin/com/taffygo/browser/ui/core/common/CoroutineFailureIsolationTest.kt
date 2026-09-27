// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.common

import com.taffygo.browser.ui.core.common.di.TaffyProcessLifetime
import com.taffygo.browser.ui.core.common.di.TaffyProfileLifetime
import com.taffygo.browser.ui.core.common.di.TaffyTabLifetime
import com.taffygo.browser.ui.core.common.di.TaffyWindowLifetime
import com.taffygo.browser.ui.core.common.di.CoroutineScopeProvider
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.launch
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.TestCoroutineScheduler
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

@OptIn(ExperimentalCoroutinesApi::class)
class CoroutineFailureIsolationTest {
    @Test
    fun `every Chromium lifetime records a content-free failure and keeps siblings alive`() {
        val scheduler = TestCoroutineScheduler()
        val dispatcher = StandardTestDispatcher(scheduler)
        val dispatchers = TestDispatchers(dispatcher)
        val recorded = mutableListOf<CoroutineFailureScope>()
        val sink = CoroutineFailureSink(recorded::add)
        val lifetimes = listOf(
            TaffyProcessLifetime(dispatchers, sink),
            TaffyProfileLifetime(dispatchers, sink),
            TaffyWindowLifetime(dispatchers, sink),
            TaffyTabLifetime(dispatchers, sink),
        )
        var siblingsCompleted = 0

        lifetimes.forEach { lifetime ->
            lifetime.scope.launch { error("content-that-must-not-reach-the-sink") }
            lifetime.scope.launch { siblingsCompleted += 1 }
        }
        scheduler.advanceUntilIdle()

        assertEquals(CoroutineFailureScope.entries, recorded)
        assertEquals(lifetimes.size, siblingsCompleted)
        lifetimes.forEach(AutoCloseable::close)
    }

    @Test
    fun `application scope uses the same terminal failure mapping`() {
        val scheduler = TestCoroutineScheduler()
        val dispatcher = StandardTestDispatcher(scheduler)
        val recorded = mutableListOf<CoroutineFailureScope>()
        val scope = CoroutineScopeProvider.provideApplicationScope(
            TestDispatchers(dispatcher),
            CoroutineFailureSink(recorded::add),
        )
        var siblingCompleted = false

        scope.launch { throw IllegalStateException("untrusted-message") }
        scope.launch { siblingCompleted = true }
        scheduler.advanceUntilIdle()

        assertEquals(listOf(CoroutineFailureScope.PROCESS), recorded)
        assertTrue(siblingCompleted)
    }

    private class TestDispatchers(
        override val default: CoroutineDispatcher,
    ) : AppDispatchers {
        override val io: CoroutineDispatcher = default
        override val main: CoroutineDispatcher = Dispatchers.Unconfined
    }
}

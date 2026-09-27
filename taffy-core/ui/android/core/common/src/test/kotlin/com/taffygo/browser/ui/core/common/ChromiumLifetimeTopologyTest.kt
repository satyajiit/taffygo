// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.common

import com.taffygo.browser.ui.core.common.di.TaffyProfileIdentity
import com.taffygo.browser.ui.core.common.di.TaffyProfileLifetime
import com.taffygo.browser.ui.core.common.di.TaffyTabLifetime
import com.taffygo.browser.ui.core.common.di.TaffyWindowLifetime
import java.io.Closeable
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class ChromiumLifetimeTopologyTest {
    private val dispatchers = TestDispatchers()
    private val sink = CoroutineFailureSink { error("Unexpected lifetime failure") }

    @Test
    fun `window rotation and tab movement preserve sibling ownership`() {
        val profile = TaffyProfileLifetime(dispatchers, sink)
        val firstWindow = TaffyWindowLifetime(dispatchers, sink)
        val secondWindow = TaffyWindowLifetime(dispatchers, sink)
        val movableTab = TaffyTabLifetime(dispatchers, sink)
        val tabResource = RecordingCloseable()
        movableTab.own(tabResource)

        firstWindow.close()

        assertFalse(firstWindow.isActive())
        assertTrue(secondWindow.isActive())
        assertTrue(movableTab.isActive())
        assertEquals(0, tabResource.closeCount)

        val rotatedWindow = TaffyWindowLifetime(dispatchers, sink)
        assertTrue(rotatedWindow.isActive())
        assertTrue(movableTab.isActive())

        movableTab.close()
        movableTab.close()
        assertEquals(1, tabResource.closeCount)
        profile.close()
        secondWindow.close()
        rotatedWindow.close()
    }

    @Test
    fun `regular and private profile identities and jobs are isolated`() {
        val regularIdentity = TaffyProfileIdentity("same-browser-profile", private = false)
        val privateIdentity = TaffyProfileIdentity("same-browser-profile", private = true)
        val regular = TaffyProfileLifetime(dispatchers, sink)
        val privateProfile = TaffyProfileLifetime(dispatchers, sink)

        assertNotEquals(regularIdentity, privateIdentity)
        privateProfile.close()
        assertFalse(privateProfile.isActive())
        assertTrue(regular.isActive())
        regular.close()
    }

    @Test
    fun `one broken resource cannot strand the rest of a lifetime`() {
        val lifetime = TaffyWindowLifetime(dispatchers, sink)
        val first = RecordingCloseable()
        val last = RecordingCloseable()
        lifetime.own(first)
        lifetime.own(Closeable { throw IllegalStateException("first close failed") })
        lifetime.own(Closeable { throw IllegalArgumentException("second close failed") })
        lifetime.own(last)

        val failure = runCatching(lifetime::close).exceptionOrNull()

        assertTrue(failure is IllegalArgumentException)
        assertEquals("second close failed", failure?.message)
        assertEquals(listOf("first close failed"), failure?.suppressed?.map { it.message })
        assertEquals(1, first.closeCount)
        assertEquals(1, last.closeCount)
        assertFalse(lifetime.isActive())
        lifetime.close()
    }

    private fun com.taffygo.browser.ui.core.common.di.TaffyLifetime.isActive(): Boolean =
        scope.coroutineContext[Job]?.isActive == true

    private class RecordingCloseable : Closeable {
        var closeCount = 0

        override fun close() {
            closeCount += 1
        }
    }

    private class TestDispatchers : AppDispatchers {
        override val main: CoroutineDispatcher = Dispatchers.Unconfined
        override val default: CoroutineDispatcher = Dispatchers.Unconfined
        override val io: CoroutineDispatcher = Dispatchers.Unconfined
    }
}

// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.browser

import com.taffygo.browser.ui.core.browser.internal.FakeBrowserMediator
import com.taffygo.browser.ui.core.browser.internal.SeedContent
import com.taffygo.browser.ui.core.common.Clock
import com.taffygo.browser.ui.core.model.DownloadAction
import com.taffygo.browser.ui.core.model.DownloadState
import com.taffygo.browser.ui.core.model.PageLoadFailure
import kotlinx.coroutines.test.runTest
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * The UI host implementation of the browser seam.
 *
 * It has no web engine, and the point of these tests is that it never pretends
 * otherwise: a host either resolves to a fixture page or reports why it did not.
 */
class FakeBrowserMediatorTest {

    private val mediator = FakeBrowserMediator(FixedClock())

    @Test
    fun `a fixture host loads and reports its title`() = runTest {
        mediator.navigateTo(SeedContent.SHOP_HOST)

        val navigation = mediator.navigation.value
        assertEquals(SeedContent.SHOP_HOST, navigation.host)
        assertEquals("Product listing", navigation.title)
        assertNull(navigation.failure)
        assertFalse(navigation.isLoading)
    }

    @Test
    fun `the unreachable host reports why it did not load`() = runTest {
        mediator.navigateTo(SeedContent.UNREACHABLE_HOST)

        assertEquals(PageLoadFailure.NAME_NOT_RESOLVED, mediator.navigation.value.failure)
    }

    @Test
    fun `going back is offered only once there is somewhere to go`() = runTest {
        assertFalse(mediator.goBack())

        mediator.navigateTo(SeedContent.SHOP_HOST)

        assertTrue(mediator.navigation.value.canGoBack)
        assertTrue(mediator.goBack())
        assertEquals(SeedContent.DOCS_HOST, mediator.navigation.value.host)
    }

    @Test
    fun `going back creates forward history and going forward consumes it`() = runTest {
        mediator.navigateTo(SeedContent.SHOP_HOST)
        assertTrue(mediator.goBack())

        assertTrue(mediator.navigation.value.canGoForward)
        assertTrue(mediator.goForward())
        assertEquals(SeedContent.SHOP_HOST, mediator.navigation.value.host)
        assertFalse(mediator.navigation.value.canGoForward)
    }

    @Test
    fun `opening a tab selects it and leaves exactly one selected`() = runTest {
        val id = mediator.openTab(SeedContent.REVIEWS_HOST)

        val tabs = mediator.tabs.value
        assertEquals(1, tabs.count { it.isSelected })
        assertEquals(id, tabs.single { it.isSelected }.id)
    }

    @Test
    fun `closing the last tab opens a new one rather than leaving no window`() = runTest {
        mediator.tabs.value.forEach { mediator.closeTab(it.id) }

        assertEquals(1, mediator.tabs.value.size)
        assertEquals(1, mediator.tabs.value.count { it.isSelected })
    }

    @Test
    fun `closing the selected tab leaves another one selected`() = runTest {
        val selected = mediator.tabs.value.single { it.isSelected }

        mediator.closeTab(selected.id)

        assertEquals(1, mediator.tabs.value.count { it.isSelected })
        assertTrue(mediator.tabs.value.none { it.id == selected.id })
    }

    @Test
    fun `a finished download cannot be paused`() = runTest {
        val complete = mediator.downloads.value.first { it.fraction == 1.0f }

        assertFalse(mediator.performDownloadAction(complete.id, DownloadAction.PAUSE))

        assertEquals(
            complete.state,
            mediator.downloads.value.single { it.id == complete.id }.state,
        )
    }

    @Test
    fun `pause and resume return truthful results and rotate the advertised action`() = runTest {
        val running = mediator.downloads.value.single { it.state == DownloadState.RUNNING }

        assertTrue(mediator.performDownloadAction(running.id, DownloadAction.PAUSE))
        val paused = mediator.downloads.value.single { it.id == running.id }
        assertEquals(DownloadState.PAUSED, paused.state)
        assertEquals(
            setOf(DownloadAction.RESUME, DownloadAction.CANCEL),
            paused.allowedActions,
        )
        assertFalse(mediator.performDownloadAction(running.id, DownloadAction.PAUSE))

        assertTrue(mediator.performDownloadAction(running.id, DownloadAction.RESUME))
        val resumed = mediator.downloads.value.single { it.id == running.id }
        assertEquals(DownloadState.RUNNING, resumed.state)
        assertEquals(
            setOf(DownloadAction.PAUSE, DownloadAction.CANCEL),
            resumed.allowedActions,
        )
    }

    @Test
    fun `every seeded host is a fixture name and never a live site`() {
        val hosts = mediator.tabs.value.map { it.host } + mediator.downloads.value.map { it.host }

        assertTrue(hosts.all { it.endsWith(".example.test") })
    }

    @Test
    fun `the first load from a blank tab does not leave a back entry`() = runTest {
        mediator.openTab("")
        assertFalse(mediator.navigation.value.canGoBack)

        mediator.navigateTo(SeedContent.SHOP_HOST)

        assertEquals(SeedContent.SHOP_HOST, mediator.navigation.value.host)
        assertFalse(mediator.navigation.value.canGoBack)
        assertFalse(mediator.goBack())
        assertEquals(SeedContent.SHOP_HOST, mediator.navigation.value.host)
    }

    @Test
    fun `opening a blank tab always mints another tab`() = runTest {
        val before = mediator.tabs.value.size

        mediator.openTab("")
        mediator.openTab("")

        assertEquals(before + 2, mediator.tabs.value.size)
        assertEquals(2, mediator.tabs.value.count { it.hasBeenNowhere })
    }

    private class FixedClock : Clock {
        override fun nowEpochMillis(): Long = 1_767_225_600_000L
    }
}

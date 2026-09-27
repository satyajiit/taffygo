// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.browser

import com.taffygo.browser.ui.core.browser.internal.DisconnectedBrowserMediator
import com.taffygo.browser.ui.core.browser.internal.SeedContent
import com.taffygo.browser.ui.core.model.DownloadAction
import com.taffygo.browser.ui.core.model.DownloadId
import com.taffygo.browser.ui.core.model.TabId
import kotlinx.coroutines.test.runTest
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * The browser seam with no browser behind it.
 *
 * `FakeBrowserMediatorTest` proves the UI host fixture is honest about having
 * no engine by proving every host it names is a fixture host. This proves the
 * complementary thing, and it is the one a shipping build depends on: that this
 * implementation names no host at all. A surface driven by this one has nothing
 * to draw, which is the correct picture of a browser that has not started yet.
 */
class DisconnectedBrowserMediatorTest {

    private val mediator = DisconnectedBrowserMediator()

    /** Every command on the seam, so no assertion below is about an idle object. */
    private suspend fun driveEveryCommand() {
        mediator.openTab(SeedContent.DOCS_HOST)
        mediator.openTab(SeedContent.SHOP_HOST, isPrivate = true)
        mediator.selectTab(TabId("tab_1"))
        mediator.closeTab(TabId("tab_1"))
        mediator.navigateTo(SeedContent.REVIEWS_HOST)
        mediator.goBack()
        mediator.goForward()
        mediator.reload()
        mediator.stopLoading()
        mediator.performDownloadAction(DownloadId("download_1"), DownloadAction.PAUSE)
        mediator.performDownloadAction(DownloadId("download_1"), DownloadAction.RESUME)
    }

    @Test
    fun `there are no tabs, and no command creates one`() = runTest {
        assertTrue(mediator.tabs.value.isEmpty())

        driveEveryCommand()

        assertTrue(mediator.tabs.value.isEmpty())
    }

    @Test
    fun `opening a tab returns an identifier that names no tab`() = runTest {
        val id = mediator.openTab(SeedContent.DOCS_HOST)

        // The signature has to return an id. This one matches nothing, and
        // there is nothing for it to match: a caller that looks it up finds no
        // tab rather than a tab that was never opened.
        assertTrue(mediator.tabs.value.none { it.id == id })
    }

    @Test
    fun `the navigation state is blank, and no command fills it in`() = runTest {
        driveEveryCommand()

        val navigation = mediator.navigation.value
        assertEquals("", navigation.host)
        assertEquals("", navigation.title)
        assertFalse(navigation.canGoBack)
        assertFalse(navigation.canGoForward)
        // Not loading, and not failed either. A browser that has not started is
        // neither: claiming a failure would be as wrong as claiming a page.
        assertFalse(navigation.isLoading)
        assertNull(navigation.failure)
    }

    @Test
    fun `there are no downloads, and a pause is truthfully refused`() = runTest {
        assertTrue(mediator.downloads.value.isEmpty())

        assertFalse(
            mediator.performDownloadAction(DownloadId("download_1"), DownloadAction.PAUSE),
        )

        assertTrue(mediator.downloads.value.isEmpty())
    }

    @Test
    fun `going back reports that there is nowhere to go`() = runTest {
        assertFalse(mediator.goBack())

        driveEveryCommand()

        assertFalse(mediator.goBack())
    }

    @Test
    fun `going forward reports that there is nowhere to go`() = runTest {
        assertFalse(mediator.goForward())

        driveEveryCommand()

        assertFalse(mediator.goForward())
    }

    @Test
    fun `no host or title it reports is a fixture host`() = runTest {
        driveEveryCommand()

        // The predicate has to be able to fail, so state what it catches: the
        // seeded hosts of the UI host fixture are exactly the shape being ruled
        // out here.
        assertTrue(SeedContent.DOCS_HOST.endsWith(FIXTURE_SUFFIX))

        val reported = mediator.tabs.value.flatMap { listOf(it.host, it.title) } +
            mediator.downloads.value.flatMap { listOf(it.host, it.fileName) } +
            listOf(mediator.navigation.value.host, mediator.navigation.value.title)

        assertTrue(reported.none { it.endsWith(FIXTURE_SUFFIX) })
        // Stronger than the line above, and the reason it can be stated: there
        // is no string here to inspect at all.
        assertTrue(reported.all { it.isEmpty() })
    }

    private companion object {
        /** The reserved suffix every UI host fixture host carries. */
        const val FIXTURE_SUFFIX = ".example.test"
    }
}

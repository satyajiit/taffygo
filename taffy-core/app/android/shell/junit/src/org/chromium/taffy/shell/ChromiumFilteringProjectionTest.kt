// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.shell

import com.taffygo.browser.ui.core.browser.FilteringSettings
import org.chromium.base.test.BaseRobolectricTestRunner
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith

/**
 * The late-opened filtering planes, driven without a browser behind them.
 *
 * A tab is a `String` here and one starting with `private-` is a private tab,
 * which is the whole of what this class knows about tabs. The routing tests are
 * the important ones: decision 0128 exists because a private tab's allowance
 * reached the regular profile's list, and nothing on a host can prove that but
 * these.
 */
@RunWith(BaseRobolectricTestRunner::class)
class ChromiumFilteringProjectionTest {

    @Test
    fun `a plane that cannot open yet refuses every command and invents no fact`() {
        var attempts = 0
        val projection = ChromiumFilteringProjection<String>(isPrivate = ::isPrivate) {
            attempts += 1
            null
        }

        assertFalse(projection.ensureFilteringBridge())
        projection.refreshSettings()
        projection.setEnabled(false)
        projection.flushCountFor("tab-1")

        assertEquals(FilteringSettings(), projection.settings.value)
        assertNull(projection.settings.value.blockedThisWeek)
        assertFalse(projection.setProfileSiteException("docs.example", allow = true))
        assertFalse(projection.setSiteException("tab-1", "docs.example", allow = true))
        assertFalse(projection.isActiveFor("tab-1"))
        assertFalse(projection.isExceptedFor("tab-1"))
        assertEquals(0, projection.blockedCountFor("tab-1"))
        // Retried on every refresh, because a model's profile arrives with
        // native initialization and not before.
        assertFalse(projection.ensureFilteringBridge())
        assertTrue(attempts > 1)
    }

    @Test
    fun `only the refresh that opens the regular plane reports that it opened it`() {
        val planes = FakePlanes()
        val projection = planes.projection()

        assertTrue(projection.ensureFilteringBridge())
        assertFalse(projection.ensureFilteringBridge())
        assertFalse(projection.ensureFilteringBridge())
        assertEquals(1, planes.opens(ChromiumFilteringProjection.Plane.REGULAR))
        // Nothing has asked anything of a private tab, so no private profile
        // has been reached for.
        assertEquals(0, planes.opens(ChromiumFilteringProjection.Plane.PRIVATE))
    }

    @Test
    fun `an opened plane is projected whole and its numbers are never derived`() {
        val planes = FakePlanes()
        val regular = planes.regular
        regular.enabled = false
        regular.blockedTotal = 91
        regular.blockedThisWeek = null
        regular.minimumSitesThisWeek = null
        val projection = planes.projection()

        projection.ensureFilteringBridge()
        projection.refreshSettings()

        val settings = projection.settings.value
        assertFalse(settings.enabled)
        assertEquals(listOf("docs.example", "news.example"), settings.exceptionHosts)
        assertEquals(91L, settings.blockedTotal)
        // No window has started. A missing week is not a zero and is never
        // taken from the lifetime total.
        assertNull(settings.blockedThisWeek)
        assertNull(settings.minimumSitesThisWeek)
    }

    @Test
    fun `a count-only publication reuses the hosts and a posture edit re-reads them`() {
        val planes = FakePlanes()
        val regular = planes.regular
        val projection = planes.projection()
        projection.ensureFilteringBridge()

        projection.refreshSettings()
        regular.blockedTotal = 12
        projection.refreshSettings()
        assertEquals(1, regular.hostReads)
        assertEquals(12L, projection.settings.value.blockedTotal)

        regular.hosts = arrayOf("docs.example")
        regular.postureRevision = 2
        projection.refreshSettings()
        assertEquals(2, regular.hostReads)
        assertEquals(listOf("docs.example"), projection.settings.value.exceptionHosts)
    }

    @Test
    fun `commands and per-tab reads reach exactly the opened plane`() {
        val planes = FakePlanes()
        val regular = planes.regular
        regular.activeTabs = setOf("tab-1")
        regular.exceptedTabs = setOf("tab-1")
        regular.blockedCounts = mapOf("tab-1" to 7)
        val projection = planes.projection()
        projection.ensureFilteringBridge()

        projection.setEnabled(false)
        projection.flushCountFor("tab-1")
        val recorded = projection.setProfileSiteException("news.example", allow = true)
        regular.exceptionAnswer = false
        val refused = projection.setProfileSiteException("news.example/path", allow = true)

        assertEquals(listOf(false), regular.enabledWrites)
        assertEquals(listOf("tab-1"), regular.flushed)
        assertTrue(recorded)
        assertFalse(refused)
        assertEquals(
            listOf("news.example" to true, "news.example/path" to true),
            regular.exceptionWrites,
        )
        assertTrue(projection.isActiveFor("tab-1"))
        assertTrue(projection.isExceptedFor("tab-1"))
        assertFalse(projection.isActiveFor("tab-2"))
        assertFalse(projection.isExceptedFor("tab-2"))
        assertEquals(7, projection.blockedCountFor("tab-1"))
        assertEquals(0, projection.blockedCountFor("tab-2"))
    }

    @Test
    fun `a private tab's allowance is written to the private plane and to nothing else`() {
        val planes = FakePlanes()
        val projection = planes.projection()
        projection.ensureFilteringBridge()

        assertTrue(projection.setSiteException("private-1", "news.example", allow = true))

        assertEquals(listOf("news.example" to true), planes.privatePlane.exceptionWrites)
        assertTrue(planes.regular.exceptionWrites.isEmpty())
    }

    @Test
    fun `the settings screen's own command never follows a selected private tab`() {
        val planes = FakePlanes()
        val projection = planes.projection()
        projection.ensureFilteringBridge()
        // A private tab has already brought its plane into existence.
        projection.isActiveFor("private-1")

        assertTrue(projection.setProfileSiteException("news.example", allow = false))

        assertEquals(listOf("news.example" to false), planes.regular.exceptionWrites)
        assertTrue(planes.privatePlane.exceptionWrites.isEmpty())
    }

    @Test
    fun `a private tab's facts come from the private plane`() {
        val planes = FakePlanes()
        planes.regular.activeTabs = setOf("private-1")
        planes.regular.exceptedTabs = setOf("private-1")
        planes.regular.blockedCounts = mapOf("private-1" to 9)
        planes.privatePlane.blockedCounts = mapOf("private-1" to 2)
        val projection = planes.projection()
        projection.ensureFilteringBridge()

        // The regular plane would have answered yes to all three. It is not
        // asked: the tab decides which plane answers.
        assertFalse(projection.isActiveFor("private-1"))
        assertFalse(projection.isExceptedFor("private-1"))
        assertEquals(2, projection.blockedCountFor("private-1"))
    }

    @Test
    fun `nothing a private plane holds is ever published`() {
        val planes = FakePlanes()
        planes.privatePlane.hosts = arrayOf("private-only.example")
        planes.privatePlane.blockedTotal = 500
        val projection = planes.projection()
        projection.ensureFilteringBridge()
        projection.setSiteException("private-1", "private-only.example", allow = true)
        projection.refreshSettings()

        val settings = projection.settings.value
        assertEquals(listOf("docs.example", "news.example"), settings.exceptionHosts)
        assertEquals(4L, settings.blockedTotal)
        // The private plane's host list is never read at all, so it cannot
        // reach a cache, a projection, or an accessibility dump.
        assertEquals(0, planes.privatePlane.hostReads)
    }

    @Test
    fun `closing the private plane leaves the regular one open and publishing`() {
        val planes = FakePlanes()
        val projection = planes.projection()
        projection.ensureFilteringBridge()
        projection.isActiveFor("private-1")
        projection.refreshSettings()

        projection.closePrivatePlane()

        assertEquals(1, planes.privatePlane.closes)
        assertEquals(0, planes.regular.closes)
        assertEquals(
            listOf("docs.example", "news.example"),
            projection.settings.value.exceptionHosts,
        )
        // The next private tab gets a new plane, which is the point: nothing
        // the last one held survives it.
        projection.isActiveFor("private-2")
        assertEquals(2, planes.opens(ChromiumFilteringProjection.Plane.PRIVATE))
    }

    @Test
    fun `close shuts every plane once, keeps what was published, and forgets the posture`() {
        val planes = FakePlanes()
        val projection = planes.projection()
        projection.ensureFilteringBridge()
        projection.isActiveFor("private-1")
        projection.refreshSettings()

        projection.close()
        projection.close()

        assertEquals(1, planes.regular.closes)
        assertEquals(1, planes.privatePlane.closes)
        // A teardown is not a filtering posture: every surface still
        // collecting keeps the last true one rather than a final empty one.
        assertEquals(
            listOf("docs.example", "news.example"),
            projection.settings.value.exceptionHosts,
        )
        assertFalse(projection.setProfileSiteException("docs.example", allow = true))
        assertTrue(planes.regular.exceptionWrites.isEmpty())

        // The next plane is a different plane. Its posture revision means
        // nothing to the cache the closed one filled.
        assertTrue(projection.ensureFilteringBridge())
        projection.refreshSettings()
        assertEquals(2, planes.regular.hostReads)
    }

    private fun isPrivate(tab: String) = tab.startsWith("private-")

    /** Both planes, and how many times each was opened. */
    private class FakePlanes {
        val regular = FakePlane()
        val privatePlane = FakePlane()
        private val opened = mutableMapOf<ChromiumFilteringProjection.Plane, Int>()

        fun opens(plane: ChromiumFilteringProjection.Plane): Int = opened[plane] ?: 0

        fun projection() = ChromiumFilteringProjection<String>(
            isPrivate = { it.startsWith("private-") },
        ) { plane ->
            opened[plane] = opens(plane) + 1
            when (plane) {
                ChromiumFilteringProjection.Plane.REGULAR -> regular.seam()
                ChromiumFilteringProjection.Plane.PRIVATE -> privatePlane.seam()
            }
        }
    }

    /** One filtering plane, standing in for the browser's own bridge. */
    private class FakePlane {
        var enabled = true
        var postureRevision = 1L
        var hosts = arrayOf("docs.example", "news.example")
        var hostReads = 0
        var blockedTotal = 4L
        var blockedThisWeek: Long? = 3L
        var minimumSitesThisWeek: Int? = 2
        var activeTabs = emptySet<String>()
        var exceptedTabs = emptySet<String>()
        var blockedCounts = emptyMap<String, Int>()
        var exceptionAnswer = true
        var closes = 0
        val enabledWrites = mutableListOf<Boolean>()
        val exceptionWrites = mutableListOf<Pair<String, Boolean>>()
        val flushed = mutableListOf<String>()

        fun seam(): ChromiumFilteringProjection.Seam<String> =
            ChromiumFilteringProjection.Seam(
                isEnabled = { enabled },
                setEnabled = { enabledWrites += it },
                setSiteException = { host, allow ->
                    exceptionWrites += host to allow
                    exceptionAnswer
                },
                siteExceptions = {
                    hostReads += 1
                    hosts
                },
                postureRevision = { postureRevision },
                blockedTotal = { blockedTotal },
                blockedThisWeek = { blockedThisWeek },
                minimumSitesThisWeek = { minimumSitesThisWeek },
                isActiveFor = { tab -> tab in activeTabs },
                isExceptedFor = { tab -> tab in exceptedTabs },
                blockedCountFor = { tab -> blockedCounts[tab] ?: 0 },
                flushCountFor = { tab -> flushed += tab },
                close = { closes += 1 },
            )
    }
}

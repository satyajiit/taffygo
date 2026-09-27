// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.shell

import android.graphics.Bitmap
import com.taffygo.browser.ui.core.browser.TabArtwork
import com.taffygo.browser.ui.core.model.TabId
import org.chromium.base.Callback
import org.chromium.base.UserDataHost
import org.chromium.base.supplier.ObservableSuppliers
import org.chromium.base.test.BaseRobolectricTestRunner
import org.chromium.chrome.browser.tab.Tab
import org.chromium.chrome.browser.tab.TabObserver
import org.chromium.chrome.browser.tab.TabSelectionType
import org.chromium.chrome.browser.tab_ui.TabContentManager
import org.chromium.chrome.browser.tabmodel.TabModel
import org.chromium.chrome.browser.tabmodel.TabModelSelector
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertSame
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.mockito.Answers
import org.mockito.Mockito.doAnswer
import org.mockito.Mockito.mock
import org.mockito.Mockito.`when`
import org.robolectric.shadows.ShadowLooper

/** Exercises the real observer registrations; the page never has to be hidden for its first preview. */
@RunWith(BaseRobolectricTestRunner::class)
class TaffyTabArtworkObserverTest {
    @Test
    fun firstPaintPublishesTheSelectedPagesPreviewWithoutHidingIt() {
        val fixture = Fixture()
        try {
            fixture.answerReads(null)
            assertNull(fixture.latest.thumbnail)
            assertTrue(fixture.captured.isEmpty())

            fixture.events.didFirstVisuallyNonEmptyPaint(fixture.tab)

            assertEquals(listOf(fixture.tab), fixture.captured)
            assertEquals(listOf("capture", "read"), fixture.operations)
            val preview = Bitmap.createBitmap(36, 64, Bitmap.Config.ARGB_8888)
            fixture.answerReads(preview)
            assertSame(preview, fixture.latest.thumbnail)
        } finally {
            fixture.observer.destroy()
        }
    }

    @Test
    fun anEarlierDiskMissCannotReplaceThePaintedPreview() {
        val fixture = Fixture()
        try {
            val earlierReads = fixture.reads.toList()
            fixture.reads.clear()
            fixture.events.didFirstVisuallyNonEmptyPaint(fixture.tab)
            assertEquals(1, fixture.reads.size)

            val preview = Bitmap.createBitmap(36, 64, Bitmap.Config.ARGB_8888)
            fixture.answerReads(preview)
            earlierReads.forEach { it.onResult(null) }
            assertSame(preview, fixture.latest.thumbnail)
        } finally {
            fixture.observer.destroy()
        }
    }

    @Test
    fun showingLoadingAndEditingPageMetadataDoNotCaptureTheInitialBlankPage() {
        val fixture = Fixture()
        try {
            fixture.events.onShown(fixture.tab, TabSelectionType.FROM_USER)
            fixture.events.onUrlUpdated(fixture.tab)
            fixture.events.onFaviconUpdated(fixture.tab, null, null)
            fixture.events.onLoadProgressChanged(fixture.tab, 0.5f)
            fixture.events.onPageLoadFinished(fixture.tab, fixture.tab.url)
            fixture.events.onLoadStopped(fixture.tab, true)
            assertTrue(fixture.captured.isEmpty())
        } finally {
            fixture.observer.destroy()
        }
    }

    @Test
    fun firstPaintDoesNotCapturePrivateBackgroundOrNonWebTabs() {
        val fixtures = listOf(
            Fixture(isPrivate = true),
            Fixture(isSelected = false),
            Fixture(url = ""),
            Fixture(url = "chrome://newtab"),
        )
        try {
            fixtures.forEach { fixture ->
                fixture.events.didFirstVisuallyNonEmptyPaint(fixture.tab)
                assertTrue(fixture.captured.isEmpty())
            }
        } finally {
            fixtures.forEach { it.observer.destroy() }
        }
    }

    @Test
    fun aPaintReadCannotPublishAfterTheObserverIsDestroyed() {
        val fixture = Fixture()
        try {
            fixture.answerReads(null)
            fixture.events.didFirstVisuallyNonEmptyPaint(fixture.tab)
            assertEquals(1, fixture.reads.size)
            val publicationCount = fixture.publications.size
            fixture.observer.destroy()
            fixture.answerReads(Bitmap.createBitmap(36, 64, Bitmap.Config.ARGB_8888))
            assertEquals(publicationCount, fixture.publications.size)
        } finally {
            fixture.observer.destroy()
        }
    }

    private class Fixture(
        isPrivate: Boolean = false,
        isSelected: Boolean = true,
        url: String = "https://shop.example.test/phone",
    ) {
        val captured = mutableListOf<Tab>()
        val reads = mutableListOf<Callback<Bitmap?>>()
        val operations = mutableListOf<String>()
        val publications = mutableListOf<Map<TabId, TabArtwork>>()
        private val registered = mutableListOf<TabObserver>()
        val tab = mock(Tab::class.java) { call ->
            if (call.method.name == "addObserver") registered += call.getArgument<TabObserver>(0)
            Answers.RETURNS_DEFAULTS.answer(call)
        }
        private val model = mock(TabModel::class.java)
        private val selector = mock(TabModelSelector::class.java)
        private val cache = mock(TabContentManager::class.java) { call ->
            when (call.method.name) {
                "cacheTabThumbnail" -> {
                    captured += call.getArgument<Tab>(0)
                    operations += "capture"
                }
                "getTabThumbnailWithCallback" -> {
                    assertEquals(17, call.getArgument<Int>(0))
                    reads += call.getArgument<Callback<Bitmap?>>(2)
                    operations += "read"
                }
            }
            Answers.RETURNS_DEFAULTS.answer(call)
        }
        val observer: TaffyTabArtworkObserver
        val events: TabObserver get() = registered.single()
        val latest: TabArtwork get() = publications.last().getValue(TabId("17"))

        init {
            val pageUrl = TaffyTestGurl.from(url)
            `when`(tab.id).thenReturn(17)
            `when`(tab.isInitialized).thenReturn(true)
            `when`(tab.isOffTheRecord).thenReturn(isPrivate)
            `when`(tab.url).thenReturn(pageUrl)
            `when`(tab.userDataHost).thenReturn(UserDataHost())
            `when`(model.comprehensiveModel).thenReturn(model)
            `when`(model.count).thenReturn(1)
            `when`(model.getTabAt(0)).thenReturn(tab)
            doAnswer { listOf(tab).iterator() }.`when`(model).iterator()
            `when`(selector.models).thenReturn(listOf(model))
            `when`(selector.currentTab).thenReturn(if (isSelected) tab else null)
            `when`(selector.currentTabModelSupplier).thenReturn(ObservableSuppliers.createMonotonic(model))
            observer = TaffyTabArtworkObserver(selector, cache, publications::add)
            ShadowLooper.idleMainLooper()
            operations.clear()
        }

        fun answerReads(bitmap: Bitmap?) {
            val pending = reads.toList()
            reads.clear()
            pending.forEach { it.onResult(bitmap) }
        }
    }
}

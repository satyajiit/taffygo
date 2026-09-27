// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.ui

import android.content.Context
import android.graphics.Color
import android.view.View
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.size
import androidx.compose.runtime.mutableStateOf
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.test.assertHeightIsEqualTo
import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.assertWidthIsEqualTo
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.unit.dp
import androidx.test.platform.app.InstrumentationRegistry
import com.taffygo.browser.ui.core.browser.BrowserGraph
import com.taffygo.browser.ui.core.browser.PageSurface
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertSame
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test

/**
 * The page-host seam, with no Chromium anywhere near it.
 *
 * The page this test attaches is a plain coloured `View`. That is the whole
 * value of the seam: what [TaffyPageSurface] does — put a platform view in a
 * Compose slot, size it to that slot, and give it back on the way out — is
 * provable on a device with no browser in the build. What a real engine draws
 * into that view is a separate question, and it is not this one.
 *
 * What this test does **not** establish is anything about a real
 * `SurfaceView`. A `View` composites normally; a `SurfaceView` punches through
 * to its own window layer, and its surface belongs to the window rather than to
 * the view. `HostedPageSurfaceTest` is where that half is exercised, with a
 * genuine `SurfaceView` and a real surface callback — including the property
 * this seam's own no-op detach was once wrongly credited with: that a
 * navigation away and back does not destroy the surface.
 */
class TaffyPageSurfaceTest {

    @get:Rule
    val compose = createComposeRule()

    private val context: Context
        get() = InstrumentationRegistry.getInstrumentation().targetContext

    @Test
    fun aLivePageIsAttachedAndFillsTheSlotItWasGiven() {
        val surface = RecordingPageSurface()

        compose.setContent {
            TaffyPreview(darkTheme = false) {
                Box(modifier = Modifier.size(SlotWidth, SlotHeight)) {
                    TaffyPageSurface(
                        surface = surface,
                        modifier = Modifier.fillMaxSize().testTag(PAGE_TEST_TAG),
                    )
                }
            }
        }

        compose.onNodeWithTag(PAGE_TEST_TAG)
            .assertIsDisplayed()
            .assertWidthIsEqualTo(SlotWidth)
            .assertHeightIsEqualTo(SlotHeight)

        assertEquals(1, surface.attached)
        assertEquals(0, surface.detached)

        // The view the composition holds is the one the surface handed over,
        // and it is in the window rather than merely constructed.
        val page = requireNotNull(surface.lastAttached)
        val inWindow = compose.runOnUiThread { page.isAttachedToWindow }
        assertTrue(inWindow)
    }

    @Test
    fun leavingTheCompositionGivesTheSameViewBack() {
        val surface = RecordingPageSurface()
        val showing = mutableStateOf(true)

        compose.setContent {
            TaffyPreview(darkTheme = false) {
                Box(modifier = Modifier.size(SlotWidth, SlotHeight)) {
                    if (showing.value) {
                        TaffyPageSurface(
                            surface = surface,
                            modifier = Modifier.fillMaxSize().testTag(PAGE_TEST_TAG),
                        )
                    }
                }
            }
        }

        val attached = requireNotNull(surface.lastAttached)
        compose.runOnUiThread { showing.value = false }
        compose.waitForIdle()

        compose.onNodeWithTag(PAGE_TEST_TAG).assertDoesNotExist()
        assertEquals(1, surface.detached)
        // Given back, rather than dropped. The surface is the only party that
        // knows whether what is behind that view can survive being released,
        // and it cannot decide that about a view it was never handed.
        assertSame(attached, surface.lastDetached)
    }

    @Test
    fun theDefaultSurfaceIsNotLiveAndHasNoView() {
        val fromTheGraph = BrowserGraph.pageSurface()

        assertFalse(fromTheGraph.isLive)
        assertNull(fromTheGraph.attach(context))

        var fromTheComposition: PageSurface? = null
        compose.setContent {
            TaffyPreview(darkTheme = false) {
                fromTheComposition = LocalPageSurface.current
            }
        }

        // The composition local's default is that same non-live surface, which
        // is why every preview and every semantics test in the UI host renders
        // its placeholder rather than an empty frame.
        assertFalse(requireNotNull(fromTheComposition).isLive)
    }

    /**
     * A live page that is a coloured rectangle.
     *
     * It answers `isLive = true` and always returns a view, which is exactly
     * what [PageSurface] states. It counts both halves of the pair, because
     * "attached once, detached once" is the property the seam exists to give.
     */
    private class RecordingPageSurface : PageSurface {

        var attached = 0
            private set
        var detached = 0
            private set
        var lastAttached: View? = null
            private set
        var lastDetached: View? = null
            private set

        override val isLive: Boolean = true

        override fun attach(context: Context): View {
            attached++
            return View(context).also {
                it.setBackgroundColor(Color.MAGENTA)
                lastAttached = it
            }
        }

        override fun detach(view: View) {
            detached++
            lastDetached = view
        }
    }

    private companion object {
        const val PAGE_TEST_TAG = "taffy_page_surface"

        val SlotWidth = 200.dp
        val SlotHeight = 320.dp
    }
}

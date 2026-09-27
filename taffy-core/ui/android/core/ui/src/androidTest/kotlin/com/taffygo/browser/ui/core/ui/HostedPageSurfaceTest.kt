// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.ui

import android.content.Context
import android.view.SurfaceHolder
import android.view.SurfaceView
import android.view.View
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.size
import androidx.compose.runtime.mutableStateOf
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.Constraints
import androidx.compose.ui.layout.layout
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.unit.dp
import com.taffygo.browser.ui.core.browser.PageSurface
import java.util.concurrent.atomic.AtomicInteger
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test

/**
 * The black frame on back navigation, as a test.
 *
 * A `SurfaceView`'s surface belongs to the window. Take the view out of the
 * window — which is what disposing an `AndroidView` does — and the surface is
 * destroyed; put it back and the next frame is black until the compositor has
 * drawn into the new one. That is the flash a person sees leaving Settings.
 *
 * The two tests below are the diagnosis, executable. The first mounts the page
 * inside the branch that a navigation swaps, the way screens used to, and
 * asserts the surface is destroyed. The second hoists it above that branch, the
 * way [PageSurfaceSlot] does, and asserts it is not — same navigation, same
 * `SurfaceView`, one difference.
 *
 * This is the real thing: an actual `SurfaceView` with a real surface callback,
 * which is what `TaffyPageSurfaceTest` explicitly does not claim to exercise.
 */
class HostedPageSurfaceTest {

    @get:Rule
    val compose = createComposeRule()

    @Test
    fun mountingThePageInsideTheSwappedBranchDestroysItsSurface() {
        val surface = CountingSurfaceViewPage()
        val onBrowsing = mutableStateOf(true)

        compose.setContent {
            TaffyPreview(darkTheme = false) {
                Box(modifier = Modifier.size(SlotWidth, SlotHeight)) {
                    // The shape a navigation host has without the slot: the
                    // page lives inside the branch that gets swapped.
                    if (onBrowsing.value) {
                        TaffyPageSurface(
                            surface = surface,
                            modifier = Modifier.fillMaxSize(),
                        )
                    }
                }
            }
        }
        compose.waitForIdle()
        assertTrue("no surface was ever created", surface.created.get() >= 1)

        // Navigate away and back.
        compose.runOnUiThread { onBrowsing.value = false }
        compose.waitForIdle()
        compose.runOnUiThread { onBrowsing.value = true }
        compose.waitForIdle()

        assertTrue(
            "the surface survived a navigation it should not have",
            surface.destroyed.get() >= 1,
        )
    }

    @Test
    fun hoistingThePageAboveTheSwappedBranchKeepsItsSurface() {
        val surface = CountingSurfaceViewPage()
        val onBrowsing = mutableStateOf(true)
        val slot = PageSurfaceSlot()
        val claim = Any()

        compose.setContent {
            TaffyPreview(darkTheme = false) {
                Box(modifier = Modifier.size(SlotWidth, SlotHeight)) {
                    // The shape the slot gives: composed once, placed by the
                    // reported placement, never unmounted.
                    TaffyPageSurface(
                        surface = surface,
                        modifier = Modifier.layout { measurable, constraints ->
                            val placement = slot.placement
                            val placeable = measurable.measure(
                                Constraints.fixed(constraints.maxWidth, constraints.maxHeight),
                            )
                            layout(constraints.maxWidth, constraints.maxHeight) {
                                placeable.place(
                                    0,
                                    if (placement.onScreen) 0 else constraints.maxHeight,
                                )
                            }
                        },
                    )
                    if (onBrowsing.value) {
                        Box(modifier = Modifier.fillMaxSize())
                    }
                }
            }
        }
        compose.runOnUiThread {
            slot.place(claim, topPx = 0, bottomPx = 0, slidePx = 0, groundArgb = null)
        }
        compose.waitForIdle()
        assertTrue("no surface was ever created", surface.created.get() >= 1)
        val createdBefore = surface.created.get()

        // The same navigation. The page is parked rather than unmounted.
        compose.runOnUiThread {
            onBrowsing.value = false
            slot.park(claim)
        }
        compose.waitForIdle()
        compose.runOnUiThread {
            onBrowsing.value = true
            slot.place(claim, topPx = 0, bottomPx = 0, slidePx = 0, groundArgb = null)
        }
        compose.waitForIdle()

        assertEquals("the surface was destroyed by a navigation", 0, surface.destroyed.get())
        assertEquals("a second surface was created", createdBefore, surface.created.get())
    }

    /**
     * A live page whose view is a real `SurfaceView`, counting both halves of
     * its surface's life.
     *
     * The view is created once and handed back on every attach, which is what
     * the product's own binding does — so a second create here means the
     * *surface* was recreated, never the view.
     */
    private class CountingSurfaceViewPage : PageSurface {

        val created = AtomicInteger()
        val destroyed = AtomicInteger()
        private var view: SurfaceView? = null

        override val isLive: Boolean = true

        override fun attach(context: Context): View {
            val existing = view
            if (existing != null) {
                (existing.parent as? android.view.ViewGroup)?.removeView(existing)
                return existing
            }
            val created = SurfaceView(context)
            created.holder.addCallback(
                object : SurfaceHolder.Callback {
                    override fun surfaceCreated(holder: SurfaceHolder) {
                        this@CountingSurfaceViewPage.created.incrementAndGet()
                    }

                    override fun surfaceChanged(
                        holder: SurfaceHolder,
                        format: Int,
                        width: Int,
                        height: Int,
                    ) = Unit

                    override fun surfaceDestroyed(holder: SurfaceHolder) {
                        destroyed.incrementAndGet()
                    }
                },
            )
            view = created
            return created
        }

        /** The activity keeps the page host, exactly as the product's does. */
        override fun detach(view: View) = Unit
    }

    private companion object {
        val SlotWidth = 200.dp
        val SlotHeight = 320.dp
    }
}

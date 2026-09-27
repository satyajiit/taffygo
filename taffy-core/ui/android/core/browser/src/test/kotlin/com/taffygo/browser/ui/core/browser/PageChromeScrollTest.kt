// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.browser

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertThrows
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * The four defects this policy exists to close, each asserted by name.
 *
 * All four were found on a phone on 2026-09-07 and none of them could have
 * been found here, because the answer lived in a stream of engine callbacks
 * with no seam a test could reach. This suite is that seam, and every case
 * below is a movement a finger can actually make.
 *
 * The fourth arrived after the first three were fixed and is the reason this
 * file changed shape: a rule that read the scroll *offset* at the moment a
 * gesture began could not work, because the engine does not report an offset
 * from the middle of a page. The cases that named a "you must be this far
 * down" region went with it, and the last case in this file — that nothing but
 * a gesture beginning can hide the top bar — is what holds the design together
 * in their place.
 */
class PageChromeScrollTest {

    /**
     * One bar at 56dp on a display at 3.25, which is the phone the defects
     * were found on: a document with less than 364px left to scroll never
     * loses its top bar, and the top of a document is the first four pixels.
     */
    private val bar = 182

    private val bounds = PageChromeScroll.Bounds.ofChrome(bar)

    /** A long document, well past the floor. */
    private val tall = 20_000

    private val deepIn = 5_000

    /**
     * Hidden, by a gesture that is over.
     *
     * The second call is a second downward drag and changes nothing, which is
     * the point: a gesture gets one answer, and the next gesture is what makes
     * the rule listen again. Without this line every case below would be
     * asking what happens *during* the drag that hid the bar, which is one
     * specific question and not the general one.
     */
    private fun hidden() = PageChromeScroll(bounds).apply {
        began(reachablePx = tall, directionUp = false)
        began(reachablePx = tall, directionUp = false)
    }

    // -- A page with nothing to scroll: the swipe that stranded the controls -

    @Test
    fun `a swipe on a page with nothing to scroll keeps the top bar`() {
        val scroll = PageChromeScroll(bounds)

        assertFalse(scroll.began(reachablePx = 0, directionUp = false))
        assertTrue(scroll.topBarVisible)
    }

    /**
     * A page that can scroll, but by less than the chrome is tall, is the same
     * case: there is not enough document to pay for hiding anything.
     */
    @Test
    fun `a page barely taller than the window keeps the top bar too`() {
        val scroll = PageChromeScroll(bounds)
        val barely = bounds.reachableFloorPx - 1

        assertFalse(scroll.began(reachablePx = barely, directionUp = false))
        assertTrue(scroll.topBarVisible)
    }

    /**
     * The engine acknowledges a scroll gesture and reports its direction
     * before anything asks whether the page had room to consume it, so a
     * downward swipe against the end of a short document arrives looking
     * exactly like a downward scroll of a long one. Nothing but this policy
     * tells them apart.
     */
    @Test
    fun `a page that shrinks under the reader gets its top bar back`() {
        val scroll = hidden()
        assertFalse(scroll.topBarVisible)

        assertTrue(scroll.scrolled(offsetY = deepIn, reachablePx = 0))
        assertTrue(scroll.topBarVisible)
    }

    // -- The top bar comes back ---------------------------------------------

    @Test
    fun `a scroll up brings the top bar back at once`() {
        val scroll = hidden()

        assertTrue(scroll.began(reachablePx = tall, directionUp = true))
        assertTrue(scroll.topBarVisible)
    }

    /**
     * Decision 0119 section 1: a scroll up — including a direction change
     * part-way through a gesture — or a tap brings the top bar back.
     */
    @Test
    fun `a direction change part-way through a gesture brings it back`() {
        val scroll = hidden()

        assertTrue(scroll.turned(directionUp = true))
        assertTrue(scroll.topBarVisible)
    }

    @Test
    fun `a tap brings it back`() {
        val scroll = hidden()

        assertTrue(scroll.tapped())
        assertTrue(scroll.topBarVisible)
    }

    /**
     * The state a phone was found in: the top bar hidden at the start of the
     * document, with nothing above to scroll up into and so no gesture left
     * that would return it.
     */
    @Test
    fun `arriving back at the start of the document shows the top bar`() {
        val scroll = hidden()

        assertTrue(scroll.scrolled(offsetY = 0, reachablePx = tall))
        assertTrue(scroll.topBarVisible)
    }

    /**
     * The top of the document is a tolerance and not the first pixel exactly,
     * because the offset arrives as a whole number of physical pixels and a
     * document parked at the top can report one or two of them.
     */
    @Test
    fun `the top of the document tolerates a rounded pixel`() {
        val scroll = hidden()

        assertTrue(scroll.scrolled(offsetY = bounds.atTopPx, reachablePx = tall))
        assertTrue(scroll.topBarVisible)
    }

    @Test
    fun `a page that arrives while the top bar is hidden shows it`() {
        val scroll = hidden()

        assertTrue(scroll.restarted())
        assertTrue(scroll.topBarVisible)
    }

    // -- The blink ----------------------------------------------------------

    /**
     * The compositor reports a reversal of a fraction of a pixel as a change
     * of direction. Hundreds of them used to arrive during one fling, and the
     * bars left and rejoined the screen on each one. Now every single one of
     * them can only ever mean the same thing.
     */
    @Test
    fun `a storm of direction changes settles on shown and stays there`() {
        val scroll = hidden()
        val changes = (1..400).count { step -> scroll.turned(directionUp = step % 2 == 0) }

        assertEquals(1, changes)
        assertTrue(scroll.topBarVisible)
    }

    /**
     * The same movement sampled over and over is one answer, not one per
     * sample. A caller publishes on a change, so a policy that answered true
     * every time would rebuild the address pill as fast as the engine can
     * speak.
     */
    @Test
    fun `sampling the same scroll position over and over answers once`() {
        val scroll = hidden()
        val changes = (1..400).count { scroll.scrolled(offsetY = 0, reachablePx = tall) }

        assertEquals(1, changes)
        assertTrue(scroll.topBarVisible)
    }

    @Test
    fun `an offset well inside the document never hides the top bar by itself`() {
        val scroll = PageChromeScroll(bounds)
        val changes = (1..400).count { step ->
            scroll.scrolled(offsetY = deepIn + step, reachablePx = tall)
        }

        assertEquals(0, changes)
        assertTrue(scroll.topBarVisible)
    }

    // -- Where hiding is allowed at all -------------------------------------

    /**
     * The one case where hiding is allowed, and the regression that sent this
     * file back to be rewritten.
     *
     * The room the document has left is the whole of what is asked, because it
     * is the only fact available here that is true. The scroll *offset* is not:
     * what the engine can supply at this moment is whatever it last had reason
     * to report, and for a reader in the middle of a long page that is the top,
     * since no frame metadata is sent for an ordinary scroll between a
     * document's two edges. A rule that read it concluded every gesture began
     * at the top and refused to hide anything, on any page, ever — and because
     * hiding has one door, no later sample could correct it. The absence of an
     * offset parameter here is that fix, so this test is also the reason the
     * signature must not grow one back.
     */
    @Test
    fun `a scroll down a long document hides the top bar`() {
        val scroll = PageChromeScroll(bounds)

        assertTrue(scroll.began(reachablePx = tall, directionUp = false))
        assertFalse(scroll.topBarVisible)
    }

    /**
     * One whole gesture, from a document sitting at its own top: it hides, it
     * stays hidden while the reader is inside the document, and it comes back
     * the moment they are at the beginning again. The middle assertion is the
     * one worth having — an offset sample that is not near the top must not
     * undo what the gesture just decided.
     */
    @Test
    fun `a scroll from the top hides, stays hidden, and returns at the top`() {
        val scroll = hidden()

        assertFalse(scroll.scrolled(offsetY = 40, reachablePx = tall))
        assertFalse(scroll.topBarVisible)

        assertTrue(scroll.scrolled(offsetY = 0, reachablePx = tall))
        assertTrue(scroll.topBarVisible)
    }

    // -- One answer per gesture ---------------------------------------------

    /**
     * The blink, replayed from the phone, as the thing that must not come back.
     *
     * These are the samples `TaffyPageChromeObserver` actually logged on
     * claude.ai within 45 ms of one downward drag, in this order. The document
     * had not returned to its top and the reader had not reversed; the engine
     * was settling the edge it started from. Read as movement, the fourth line
     * put the bar straight back, on every scroll.
     */
    @Test
    fun `the engine settling is not a person asking for the bar back`() {
        val scroll = PageChromeScroll(bounds)

        assertTrue(scroll.began(reachablePx = 19_170, directionUp = false))
        assertFalse(scroll.scrolled(offsetY = 44, reachablePx = 19_170))
        assertFalse(scroll.turned(directionUp = false))
        assertFalse(scroll.scrolled(offsetY = 0, reachablePx = 19_170))
        assertFalse(scroll.turned(directionUp = true))
        assertFalse(scroll.scrolled(offsetY = 9, reachablePx = 19_170))
        assertFalse(scroll.scrolled(offsetY = 421, reachablePx = 19_170))

        assertFalse(scroll.topBarVisible)
    }

    /** And the same for a show: one answer, not a conversation about it. */
    @Test
    fun `a gesture that showed the bar cannot be talked out of it either`() {
        val scroll = hidden()
        assertTrue(scroll.tapped())

        assertFalse(scroll.scrolled(offsetY = 9_000, reachablePx = tall))
        assertTrue(scroll.topBarVisible)
    }

    /**
     * The behaviour that matters, and the reason the latch is safe: a drag
     * upward is a new gesture, so it is never suppressed and the bar is back
     * before a frame is composited.
     */
    @Test
    fun `a drag upward brings it back whatever the last gesture decided`() {
        val scroll = PageChromeScroll(bounds)
        assertTrue(scroll.began(reachablePx = tall, directionUp = false))

        assertTrue(scroll.began(reachablePx = tall, directionUp = true))
        assertTrue(scroll.topBarVisible)
    }

    /**
     * The two ways through that are not a gesture, because the engine can
     * invent neither: a finger on the page, and a new document.
     */
    @Test
    fun `a tap and a new page both reach through the latch`() {
        val tapped = PageChromeScroll(bounds)
        tapped.began(reachablePx = tall, directionUp = false)
        assertTrue(tapped.tapped())

        val restarted = PageChromeScroll(bounds)
        restarted.began(reachablePx = tall, directionUp = false)
        assertTrue(restarted.restarted())
    }

    @Test
    fun `the tolerance is far under the floor`() {
        assertTrue(bounds.reachableFloorPx > bounds.atTopPx)
        assertEquals(4, bounds.atTopPx)
        assertEquals(364, bounds.reachableFloorPx)
    }

    @Test
    fun `bounds that met would be refused rather than shipped`() {
        assertThrows(IllegalArgumentException::class.java) {
            PageChromeScroll.Bounds(atTopPx = 200, reachableFloorPx = 200)
        }
    }

    // -- Hiding has one door ------------------------------------------------

    /**
     * The property the whole design rests on, asserted directly: nothing but
     * [PageChromeScroll.began] can take the top bar off the screen. If a later
     * change gives another callback that power, this fails rather than the
     * blink coming back.
     */
    @Test
    fun `no callback other than a gesture beginning can hide the top bar`() {
        val everythingElse: List<(PageChromeScroll) -> Boolean> = listOf(
            { it.turned(directionUp = false) },
            { it.turned(directionUp = true) },
            { it.scrolled(offsetY = deepIn, reachablePx = tall) },
            { it.scrolled(offsetY = 0, reachablePx = tall) },
            { it.scrolled(offsetY = deepIn, reachablePx = 0) },
            { it.tapped() },
            { it.restarted() },
        )

        for (call in everythingElse) {
            val scroll = PageChromeScroll(bounds)
            call(scroll)
            assertTrue(scroll.topBarVisible)
        }
    }
}

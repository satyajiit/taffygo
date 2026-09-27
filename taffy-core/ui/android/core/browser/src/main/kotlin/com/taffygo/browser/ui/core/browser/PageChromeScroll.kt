// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package com.taffygo.browser.ui.core.browser

/**
 * Whether the top bar is on screen, decided from the page's scroll.
 *
 * The top bar alone. The action row at the other end of screen SCR-101 is
 * always up, so the page is measured above it and nothing here has any opinion
 * about it — a bar that never moves needs no rule. A scroll down takes the
 * address pill and the overflow away; a scroll up, a direction change part-way
 * through a gesture, or a tap brings them back. This is where that becomes
 * arithmetic, in one object a host test can drive without a device, a web
 * engine or a finger.
 *
 * ## One answer per gesture
 *
 * A gesture decides once. After the answer changes, the engine's own reports
 * are ignored until the next gesture begins — or a tap, or a new page, neither
 * of which the engine can invent.
 *
 * This is not caution, it is a transcript. A downward drag on claude.ai,
 * logged from the phone on 2026-09-07, reported in the first 45 milliseconds:
 * a gesture beginning downward with 19,170 px still to scroll; an offset of
 * 44; a direction change; **an offset of 0**; a direction change upward at a
 * scroll ratio of zero; an offset of 9; and then an offset of 421. The
 * document had not returned to its own top and the reader had not reversed.
 * Everything between the first sample and the last is the engine settling —
 * a rubber-band at the edge it started from, and its own corrections — and
 * both of the rules below read it as a person asking for the bar back. So the
 * bar came back, on every single scroll, forty milliseconds after leaving.
 *
 * [began] is the one callback a settling engine cannot produce, because it
 * takes a finger. It clears the latch on the way in and is never suppressed,
 * so a drag upward brings the bar back at once — which is the behaviour that
 * matters. What is given up is a reversal *within* the drag that hid it: that
 * one waits for the finger to lift. Stated here so it stays a decision.
 *
 * ## Hiding has one door, and a finger has to open it
 *
 * That is the whole design, and it is the answer to a chrome that blinked.
 * The engine offers several ways to hear about a scroll and they describe the
 * same movement from different distances: a gesture began, a fling began, the
 * direction turned, the offset moved. While every one of them could hide the
 * chrome *and* show it, the bars alternated — a reversal of a fraction of a
 * pixel is a direction change to a compositor, and each one took the bars off
 * the screen and put them back.
 *
 * So only [began] may hide, and everything else may only show. [began] is the
 * acknowledgement of a real gesture that a real finger started, it carries the
 * one direction the engine reports accurately, and it arrives once. A gesture
 * can therefore hide the chrome at most once and show it any number of times,
 * which is not a threshold that noise has to be small enough to pass — it is a
 * shape noise cannot make.
 *
 * ## Two rules, and each is asked of a signal that actually arrives
 *
 *  - **A document that cannot scroll never hides the top bar**
 *    ([Bounds.reachableFloorPx]). Hiding is a trade — chrome for reading room
 *    — and there is nothing to trade for on a page that already fits. A swipe
 *    on a short page is a swipe against the end of the document, not a request
 *    to clear the screen, and answering it by hiding the bars strands Back,
 *    the Assistant pill and the menu with no scroll left to bring them back.
 *    The engine does not apply this test for us: a scroll gesture is
 *    acknowledged, and its direction reported, before anything asks whether
 *    the page consumed it.
 *  - **The top of a document always shows it** ([Bounds.atTopPx]). Someone who
 *    has scrolled back to the beginning is not reading; they are looking for
 *    where they are and what to do next, which is what the two bars carry.
 *    Without this the chrome can sit hidden at offset zero with no gesture left
 *    that would return it, because there is nothing above to scroll up into.
 *
 * **There is deliberately no "you must be this far down the page" rule, and the
 * reason is worth writing down, because it looks like an omission.** Such a
 * rule was written first, with Chromium's own `kCanHideRegionMinMultiplier`
 * proportion, and it did not work: the offset it would have to read is a value
 * the engine only refreshes when it sends frame metadata, and a renderer sends
 * none for an ordinary scroll down the middle of a page. It reports a change of
 * direction, the moment the document leaves or reaches its top, and every frame
 * within ten CSS pixels of an edge — and nothing in between. So the rule read a
 * scroll position from before the gesture, concluded the reader was still at
 * the top, and declined to hide the chrome; and because the door is only open
 * at the beginning of a gesture, no later sample could correct it. On a phone
 * that showed up as chrome that never hid at all, which on a page with anything
 * anchored to the bottom of its own window — a cookie banner and its buttons,
 * say — is not cosmetic: those controls are behind the action row and there is
 * no way to reach them.
 *
 * [Bounds.atTopPx] is a tolerance for rounding rather than a region, for the
 * same reason. It has to be smaller than a gesture's first movement, or a
 * downward scroll from the top would hide the chrome and the next sample would
 * put it straight back.
 *
 * Every method answers whether the visible state *changed*, so a caller
 * publishes on a change and stays quiet through everything that does not move
 * it.
 */
class PageChromeScroll(private val bounds: Bounds) {

    /**
     * The distances this policy measures in, in physical pixels.
     *
     * Pixels rather than density-independent units for two reasons: this
     * module sits beneath the design system and may not name a `Dp`, and the
     * engine reports scroll offsets in physical pixels, so converting once at
     * construction leaves one party holding a display's density instead of
     * every comparison. [ofChrome] is that conversion.
     *
     * There is no default. Every distance defaulting to zero would be a policy
     * that hides the chrome on the first downward flick of any page at any
     * offset, which is the defect this class exists to end — and as a default
     * it would read as configuration rather than as the mistake it is.
     */
    data class Bounds(
        /**
         * At or within this much of the start of the document, the chrome
         * stays. A tolerance for a rounded pixel, not a region — see the note
         * on this class about the region that was tried and removed.
         */
        val atTopPx: Int,
        /** A document that cannot scroll this far never hides the top bar. */
        val reachableFloorPx: Int,
    ) {
        init {
            require(reachableFloorPx > atTopPx) {
                "A document must be able to scroll further than the tolerance " +
                    "that keeps the chrome up: reachableFloorPx=$reachableFloorPx, " +
                    "atTopPx=$atTopPx"
            }
        }

        companion object {
            /**
             * The two distances for one display, from the height of one bar.
             *
             * The floor is two bars, and it is a margin rather than an
             * accounting identity: hiding buys back one bar of reading room,
             * and a document with only that much left to scroll is one where
             * the trade is not worth making — the reader gains a bar and
             * arrives at the end of the page in the same movement. Twice the
             * bar is the conservative side of that and is the value the phone
             * was tested at. The tolerance is four pixels, which is under any
             * real gesture — Android will not call a touch a scroll until it
             * has travelled its own slop, which is several times this — and
             * over the rounding in a scroll offset the engine reports as a
             * whole number.
             *
             * @param barHeightPx the height of one bar of chrome, in pixels.
             */
            fun ofChrome(barHeightPx: Int): Bounds = Bounds(
                atTopPx = AT_TOP_TOLERANCE_PX,
                reachableFloorPx = barHeightPx * 2,
            )

            private const val AT_TOP_TOLERANCE_PX = 4
        }
    }

    /** Whether the top bar belongs on screen right now. */
    var topBarVisible: Boolean = true
        private set

    /**
     * Whether this gesture has already had its answer.
     *
     * True from the moment the answer changes until a finger or a navigation
     * says otherwise. While it is true, [turned] and [scrolled] are ignored —
     * they are the two the engine produces while it settles, and this class's
     * own note has the transcript.
     */
    private var settled: Boolean = false


    /**
     * A scroll gesture was acknowledged, and the engine says which way.
     *
     * The only place the top bar is ever hidden. Upward is the reader asking
     * for it back and gets it at once, before a frame has been composited,
     * which is what "a scroll up brings it back" has to mean to be believed.
     * Downward hides it on any document with room to scroll.
     *
     * It takes no scroll position, and that absence is deliberate: the offset
     * available at this moment is whatever the engine last had reason to
     * report, which for a reader in the middle of a page is not where they
     * are. Only [reachablePx] is asked for, because that is a property of the
     * document rather than of the reader's place in it and does not go stale
     * the same way.
     *
     * **A fling must not be reported here.** The engine's fling-start carries
     * a direction field that no producer writes for that event — it is the
     * zero its own union was cleared to — so every fling reads as downward,
     * including a flick towards the top of the page. A fling that follows a
     * drag has already been answered by the drag that threw it, and one that
     * follows nothing is a movement this policy is content to learn about from
     * where the document ends up.
     *
     * @param reachablePx how much further the document could be scrolled.
     */
    fun began(reachablePx: Int, directionUp: Boolean): Boolean {
        // A new gesture. The engine has finished settling whatever the last
        // one decided, and is worth listening to again.
        settled = false
        if (directionUp) return show()
        if (reachablePx < bounds.reachableFloorPx) return false
        return hide()
    }

    /**
     * The movement turned around part-way through a gesture.
     *
     * Upward brings the chrome back, which is decision 0119's sentence about a
     * direction change. Downward does nothing at all, and that asymmetry is
     * what makes this callback safe to act on: the compositor calls a
     * reversal of a fraction of a pixel a change of direction as readily as it
     * calls a deliberate one, so a callback that could also hide would be a
     * blink with a different door on it.
     *
     * The asymmetry has a price and it is the one worth paying. A page that
     * shifts under the reader — an image arriving above what they are looking
     * at, and the engine holding their place by moving the offset — can report
     * a reversal nobody made, and the bar comes back in the middle of a scroll
     * down. Failing towards the bar being reachable is the right direction to
     * fail in: it carries the address, the menu and every way to see where you
     * are.
     *
     *
     * Ignored once this gesture has had its answer: the engine reports a
     * reversal at a scroll ratio of zero while it settles, and that is not a
     * person.
     */
    fun turned(directionUp: Boolean): Boolean =
        if (settled || !directionUp) false else show()

    /**
     * The document is at [offsetY], and could be scrolled [reachablePx] in all.
     *
     * Only ever shows. Two things bring the chrome back here and neither is a
     * direction: arriving near the start of the document, and a document that
     * turns out to have nothing to scroll — a page whose content shrank, or
     * one measured before its own layout had settled.
     *
     * `reachablePx` is the document's height less the viewport's: the question
     * "can this page scroll at all", asked in the only form that has an
     * answer. It is not the extent the gesture callbacks carry, which is the
     * viewport's own height and is the same on a page of one line and a page
     * of a thousand.
     */
    fun scrolled(offsetY: Int, reachablePx: Int): Boolean {
        // The engine reports the offset it rubber-banded away from before it
        // reports where the finger took the page, so a gesture that has just
        // hidden the bar sees one sample claiming the top of the document.
        if (settled) return false
        if (offsetY > bounds.atTopPx && reachablePx >= bounds.reachableFloorPx) {
            return false
        }
        return show()
    }

    /** A tap on the page. Always brings the top bar back. */
    fun tapped(): Boolean {
        settled = false
        return show()
    }

    /**
     * A different page, or the same one shown again.
     *
     * A page that arrives while the chrome is hidden would otherwise be read
     * from behind a bar that is not there, with the address of the page now
     * in front of the person nowhere on the screen.
     */
    fun restarted(): Boolean {
        settled = false
        return show()
    }

    private fun show(): Boolean = set(true)

    private fun hide(): Boolean = set(false)

    private fun set(visible: Boolean): Boolean {
        if (topBarVisible == visible) return false
        topBarVisible = visible
        // This gesture has said what it had to say.
        settled = true
        return true
    }
}

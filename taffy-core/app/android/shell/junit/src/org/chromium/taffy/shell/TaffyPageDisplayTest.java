// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.shell;

import static org.junit.Assert.assertEquals;

import org.junit.Test;
import org.junit.runner.RunWith;

import org.chromium.base.test.BaseRobolectricTestRunner;

/**
 * What the page host is told, case by case, on a laptop.
 *
 * <p><b>Why this suite exists at all.</b> The browser force-closed on every second launch, and the
 * two questions behind it were both decisions of this shape: what to show when there is no tab, and
 * whether a tab that has a page still has to be told to load it. Neither lived anywhere a test
 * could reach, so neither was tested, and the first evidence either was wrong was a process death
 * on a phone. {@link TaffyPageDisplay} is that decision pulled out of the activity; this is the
 * suite that reads it back.
 *
 * <p>It asserts outcomes rather than restating the implementation: each test names the situation a
 * person is actually in and the answer the browser owes them, and the two combinations that look
 * redundant are the ones that were wrong in the shipped build.
 */
@RunWith(BaseRobolectricTestRunner.class)
public class TaffyPageDisplayTest {

    // -----------------------------------------------------------------------
    // Nothing to show.
    // -----------------------------------------------------------------------

    @Test
    public void aBrowserWithNoTabDrawsNothing() {
        // The state a browser is in between closing its last tab and opening the next, and the
        // state the page host has to survive: passing null through to
        // ContentViewRenderView.setCurrentWebContents is what killed the process.
        assertEquals(
                TaffyPageDisplay.NOTHING,
                TaffyPageDisplay.of(
                        /* hasTab= */ false, /* hasLivePage= */ false, /* needsLoad= */ false));
    }

    @Test
    public void aTabWithNoLivePageDrawsNothing() {
        // A tab created for a lazy load holds no WebContents, so there is nothing to draw and
        // nothing to touch. Drawing nothing is the truthful answer; anything else would be a claim
        // about a page that does not exist.
        assertEquals(
                TaffyPageDisplay.NOTHING,
                TaffyPageDisplay.of(
                        /* hasTab= */ true, /* hasLivePage= */ false, /* needsLoad= */ false));
    }

    @Test
    public void aPageThatIsNotThereIsNotLoadedNoMatterWhatIsAskedAboutIt() {
        // This is the combination that matters most and reads as redundant. "Needs loading" and
        // "exists" are separate facts, and the activity acts on the first by dereferencing the
        // second: TaffyPageDisplay.PAGE_AND_START_LOAD is the signal to call
        // getNavigationController() on the WebContents. If a missing page could ever produce that
        // answer, the fix for the restart crash would have introduced a null dereference of its
        // own on the very same path.
        assertEquals(
                TaffyPageDisplay.NOTHING,
                TaffyPageDisplay.of(
                        /* hasTab= */ true, /* hasLivePage= */ false, /* needsLoad= */ true));
        assertEquals(
                TaffyPageDisplay.NOTHING,
                TaffyPageDisplay.of(
                        /* hasTab= */ false, /* hasLivePage= */ false, /* needsLoad= */ true));
    }

    @Test
    public void noTabOutranksAPageHandedInWithoutOne() {
        // Defence against a caller that reads a stale WebContents out of a tab it has already let
        // go of. There is no tab, so there is nothing to show, whatever else is true.
        assertEquals(
                TaffyPageDisplay.NOTHING,
                TaffyPageDisplay.of(
                        /* hasTab= */ false, /* hasLivePage= */ true, /* needsLoad= */ false));
    }

    // -----------------------------------------------------------------------
    // Something to show.
    // -----------------------------------------------------------------------

    @Test
    public void aPageAlreadyUnderWayIsDrawnAndLeftAlone() {
        // The ordinary case: a tab the person has been using. Starting a load here would abandon
        // whatever navigation is in flight.
        assertEquals(
                TaffyPageDisplay.PAGE,
                TaffyPageDisplay.of(
                        /* hasTab= */ true, /* hasLivePage= */ true, /* needsLoad= */ false));
    }

    @Test
    public void aRestoredPageIsDrawnAndStarted() {
        // A WebContents rebuilt from a saved session has its navigation entries and has not been
        // asked to load them, and in this activity nothing else will ever ask: TabImpl.loadIfNeeded
        // returns false before it reaches the code that would. Without this answer a restored tab
        // draws an empty frame for the rest of the process's life.
        assertEquals(
                TaffyPageDisplay.PAGE_AND_START_LOAD,
                TaffyPageDisplay.of(
                        /* hasTab= */ true, /* hasLivePage= */ true, /* needsLoad= */ true));
    }
}

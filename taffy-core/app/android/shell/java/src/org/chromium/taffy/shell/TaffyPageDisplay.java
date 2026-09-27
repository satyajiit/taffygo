// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.shell;

/**
 * What the page host is told to do about the selected tab — the whole decision, with no browser in
 * it.
 *
 * <p><b>Why this is a type and not three lines inside the activity.</b> The browser force-closed on
 * every second launch, and two of the three defects behind that were decisions of exactly this
 * shape: what to show when there is no tab, and whether a tab that has a page still needs its
 * navigation started. Neither was written down anywhere a test could reach, so neither was tested.
 * This is the shape {@code TaffyNavigationProjection} already uses for the same reason — the part
 * of a browser surface that is a decision rather than a call is separated so that it can be checked
 * on a laptop.
 *
 * <p><b>The three facts this decision is made from</b>, and why each one is a fact and not a guess:
 *
 * <ul>
 *   <li><i>Is there a tab.</i> A browser with every tab closed is a real state, and the honest
 *       answer to it is to draw nothing. It is also the state the last-tab-closed path passes
 *       through, which is why {@code ChromiumBrowserMediator.closeTab} opens the replacement before
 *       it closes the last tab rather than after.
 *   <li><i>Does that tab hold a live page.</i> A {@code Tab} whose {@code getWebContents()} is null
 *       has no view either, so there is nothing to draw and nothing to touch. After the eager
 *       restore in {@link TaffyTabCreator} no restored tab is in that state; a tab created for a
 *       lazy load still can be, and drawing nothing is the truthful answer for it.
 *   <li><i>Does the page's navigation still need to be started.</i> A {@code WebContents} restored
 *       from a saved session has its entries back and has not been asked to load them —
 *       {@code WebContentsState::RestoreContentsFromByteBufferImpl} calls
 *       {@code GetController().Restore(...)} and stops. Upstream starts the load in
 *       {@code TabImpl.restoreIfNeeded}, behind a guard that requires a {@code ChromeActivity} and
 *       therefore never runs here. The same fact is true after a renderer is killed, which is why
 *       this is asked on every show and not only on the first.
 * </ul>
 */
public enum TaffyPageDisplay {

    /** Draw nothing: there is no tab, or the tab has no live page. */
    NOTHING,

    /** Draw the tab's page. Its navigation is already under way or already finished. */
    PAGE,

    /**
     * Draw the tab's page and start its navigation.
     *
     * <p>This is the state a restored tab arrives in, and the state a tab whose renderer was killed
     * returns to. In TaffyGo it is the <i>only</i> moment either of them is loaded, because
     * {@code TabImpl.loadIfNeeded} returns false for this activity before it reaches the code that
     * would have done it.
     */
    PAGE_AND_START_LOAD;

    /**
     * The decision.
     *
     * @param hasTab whether a tab is selected at all.
     * @param hasLivePage whether that tab holds a {@code WebContents}.
     * @param needsLoad whether that page's navigation controller reports it needs to be loaded.
     * @return what the page host should be told.
     */
    public static TaffyPageDisplay of(boolean hasTab, boolean hasLivePage, boolean needsLoad) {
        if (!hasTab || !hasLivePage) return NOTHING;
        return needsLoad ? PAGE_AND_START_LOAD : PAGE;
    }
}

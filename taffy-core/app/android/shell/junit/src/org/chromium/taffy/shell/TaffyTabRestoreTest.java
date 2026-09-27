// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.shell;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import android.content.Intent;

import org.junit.Test;
import org.junit.runner.RunWith;

import org.chromium.base.test.BaseRobolectricTestRunner;
import org.chromium.chrome.browser.tab.Tab;
import org.chromium.chrome.browser.tab.TabLaunchType;
import org.chromium.content_public.browser.LoadUrlParams;

/**
 * The two choices restoring a tab makes, checked without a browser.
 *
 * <p>{@link TaffyTabCreator} rebuilds a saved tab itself rather than letting {@code TabImpl} inflate
 * a frozen one, because in this activity a frozen tab is never inflated at all. That means the small
 * decisions upstream makes inside {@code restoreFieldsFromState} and {@code unfreezeContents} are
 * now TaffyGo's, and both of them fail quietly when they are wrong: a tab silently joins a group it
 * was never in, or a session that could not be read lands on an address nobody chose. Neither shows
 * up as a crash, which is exactly why they are separated to here.
 *
 * <p>{@code Tab.INVALID_TAB_ID} is named rather than written as a number, so a change to it upstream
 * fails this suite to compile rather than passing it against the wrong constant.
 */
@RunWith(BaseRobolectricTestRunner.class)
public class TaffyTabRestoreTest {

    /** An id that is a plausible tab id and is not the one being restored. */
    private static final int ANOTHER_TAB_ID = 7;

    /** The id the tab is being restored under, throughout. */
    private static final int THIS_TAB_ID = 12;

    private static final String START_PAGE = "about:blank";

    @Test
    public void onlyPersistentRestoreRequiresLiveNewTab() {
        assertTrue(TaffyTabRestore.requiresLiveNewTab(TabLaunchType.FROM_RESTORE));
        assertFalse(TaffyTabRestore.requiresLiveNewTab(TabLaunchType.FROM_CHROME_UI));
        assertFalse(TaffyTabRestore.requiresLiveNewTab(TabLaunchType.FROM_LINK));
        assertFalse(TaffyTabRestore.requiresLiveNewTab(TabLaunchType.FROM_EXTERNAL_APP));
    }

    @Test
    public void everyTabCreatorOverloadUsedByPersistentRestoreIsOwnedHere() throws Exception {
        assertEquals(
                TaffyTabCreator.class,
                TaffyTabCreator.class
                        .getMethod("createNewTab", LoadUrlParams.class, int.class, Tab.class)
                        .getDeclaringClass());
        assertEquals(
                TaffyTabCreator.class,
                TaffyTabCreator.class
                        .getMethod(
                                "createNewTab",
                                LoadUrlParams.class,
                                int.class,
                                Tab.class,
                                int.class)
                        .getDeclaringClass());
        assertEquals(
                TaffyTabCreator.class,
                TaffyTabCreator.class
                        .getMethod(
                                "createNewTab",
                                LoadUrlParams.class,
                                String.class,
                                int.class,
                                Tab.class,
                                int.class)
                        .getDeclaringClass());
        assertEquals(
                TaffyTabCreator.class,
                TaffyTabCreator.class
                        .getMethod(
                                "createNewTab",
                                LoadUrlParams.class,
                                int.class,
                                Tab.class,
                                Intent.class)
                        .getDeclaringClass());
        assertEquals(
                TaffyTabCreator.class,
                TaffyTabCreator.class
                        .getMethod("createTabWithHistory", Tab.class, int.class)
                        .getDeclaringClass());
    }

    // -----------------------------------------------------------------------
    // Which group a restored tab belongs to.
    // -----------------------------------------------------------------------

    @Test
    public void aTabThatWasInNoGroupStaysInNoGroup() {
        // "No group" is expressed in this model as a tab whose root is itself. A saved state with
        // no root at all carries the invalid id, and passing that through would put every such tab
        // under one impossible root together — which is a group, and the wrong one.
        assertEquals(
                THIS_TAB_ID, TaffyTabRestore.rootIdFor(Tab.INVALID_TAB_ID, THIS_TAB_ID));
    }

    @Test
    public void aTabThatWasInAGroupKeepsIt() {
        // The substitution above must not reach a real saved root, or restoring a session would
        // dissolve every group in it.
        assertEquals(ANOTHER_TAB_ID, TaffyTabRestore.rootIdFor(ANOTHER_TAB_ID, THIS_TAB_ID));
    }

    @Test
    public void aTabThatWasItsOwnGroupRootStillIs() {
        assertEquals(THIS_TAB_ID, TaffyTabRestore.rootIdFor(THIS_TAB_ID, THIS_TAB_ID));
    }

    // -----------------------------------------------------------------------
    // Where a tab goes when its saved history cannot be read back.
    // -----------------------------------------------------------------------

    @Test
    public void theAddressInTheSavedStateIsPreferred() {
        // Upstream's order, kept: the address the state itself carries is the most specific thing
        // known about where this tab was.
        assertEquals(
                "https://example.test/page",
                TaffyTabRestore.fallbackUrlFor(
                        "https://example.test/page", "https://example.test/", START_PAGE));
    }

    @Test
    public void theStoresOwnRecordIsUsedWhenTheStateCarriesNoAddress() {
        // TabPersistentStoreImpl writes the tab's last known URL onto the state before handing it
        // to the creator, precisely so that an unreadable navigation history still has somewhere to
        // go. Both spellings of "carries no address" are checked, because a saved state whose
        // metadata cannot be parsed answers null and one whose metadata is empty answers "".
        assertEquals(
                "https://example.test/",
                TaffyTabRestore.fallbackUrlFor(null, "https://example.test/", START_PAGE));
        assertEquals(
                "https://example.test/",
                TaffyTabRestore.fallbackUrlFor("", "https://example.test/", START_PAGE));
    }

    @Test
    public void aTabWithNothingKnownAboutItOpensOnTheStartPage() {
        // The last resort, and the reason the start page is passed in rather than named in the
        // restore code: TaffyGo resolves no native page, so upstream's answer here — the new-tab
        // page — would be fetched as an ordinary web page and answered with an error.
        assertEquals(START_PAGE, TaffyTabRestore.fallbackUrlFor(null, null, START_PAGE));
        assertEquals(START_PAGE, TaffyTabRestore.fallbackUrlFor("", "", START_PAGE));
    }

    @Test
    public void thereIsAlwaysAnAddressToLoad() {
        // The one property the caller depends on: TaffyTabCreator hands this straight to
        // Tab.loadUrl, and an empty address there is a tab that silently never loads — the exact
        // failure this whole change exists to remove.
        for (String virtual : new String[] {null, "", "https://example.test/page"}) {
            for (String saved : new String[] {null, "", "https://example.test/"}) {
                String url = TaffyTabRestore.fallbackUrlFor(virtual, saved, START_PAGE);
                assertFalse(
                        "virtual=" + virtual + " saved=" + saved, url == null || url.isEmpty());
            }
        }
    }
}

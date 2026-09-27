// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.shell;

import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyBoolean;
import static org.mockito.ArgumentMatchers.anyInt;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.ArgumentMatchers.same;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.never;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.ArgumentMatchers;

import org.chromium.base.test.BaseRobolectricTestRunner;
import org.chromium.chrome.browser.tab.Tab;
import org.chromium.chrome.browser.tab.TabLaunchType;
import org.chromium.chrome.browser.tabmodel.TabCreator;
import org.chromium.chrome.browser.tabmodel.TabCreatorManager;
import org.chromium.chrome.browser.tabmodel.TabModelSelector;
import org.chromium.chrome.browser.util.PictureInPictureWindowOptions;
import org.chromium.content_public.browser.LoadUrlParams;
import org.chromium.content_public.browser.WebContents;
import org.chromium.ui.mojom.WindowOpenDisposition;
import org.chromium.url.GURL;

import java.util.concurrent.CompletableFuture;

@RunWith(BaseRobolectricTestRunner.class)
public class TaffyManualNavigationCoordinatorTest {
    private final long[] mNow = {1_000};
    private TaffyManualGestureGate mGate;
    private TaffyManualNavigationCoordinator mNavigation;
    private TabModelSelector mSelector;
    private TabCreator mCreator;
    private Tab mSourceTab;
    private Tab mCreatedTab;
    private WebContents mSourceContents;
    private WebContents mCreatedContents;

    @Before
    public void setUp() {
        mGate = new TaffyManualGestureGate(() -> mNow[0]);
        TaffyExternalAppHandoff handoff = new TaffyExternalAppHandoff(mGate, ignored -> true);
        mSelector = mock(TabModelSelector.class);
        TabCreatorManager creatorManager = mock(TabCreatorManager.class);
        mCreator = mock(TabCreator.class);
        mSourceTab = mock(Tab.class);
        mCreatedTab = mock(Tab.class);
        mSourceContents = mock(WebContents.class);
        mCreatedContents = mock(WebContents.class);
        when(mSelector.getCurrentTab()).thenReturn(mSourceTab);
        when(mSelector.getTabCreatorManager()).thenReturn(creatorManager);
        when(creatorManager.getTabCreator(false)).thenReturn(mCreator);
        when(mSourceTab.getId()).thenReturn(7);
        when(mSourceTab.getWebContents()).thenReturn(mSourceContents);
        when(mCreator.createTabWithWebContents(
                        same(mSourceTab),
                        anyBoolean(),
                        same(mCreatedContents),
                        eq(TabLaunchType.FROM_LINK),
                        any(GURL.class),
                        ArgumentMatchers.<CompletableFuture<Boolean>>any()))
                .thenReturn(mCreatedTab);
        mNavigation = new TaffyManualNavigationCoordinator(() -> mSelector, mGate, handoff);
    }

    @Test
    public void physicalForegroundWindowOpenAdoptsContentsExactlyOnce() {
        mNavigation.recordPageGesture(/* occurredAtMillis= */ 990);

        assertTrue(adopt(WindowOpenDisposition.NEW_FOREGROUND_TAB, true));
        assertFalse(adopt(WindowOpenDisposition.NEW_FOREGROUND_TAB, true));
        verify(mCreator)
                .createTabWithWebContents(
                        same(mSourceTab),
                        eq(false),
                        same(mCreatedContents),
                        eq(TabLaunchType.FROM_LINK),
                        any(GURL.class),
                        ArgumentMatchers.<CompletableFuture<Boolean>>any());
    }

    @Test
    public void rendererGestureWithoutAndroidProofIsBlocked() {
        assertFalse(adopt(WindowOpenDisposition.NEW_FOREGROUND_TAB, true));
        verify(mCreator, never())
                .createTabWithWebContents(any(), anyBoolean(), any(), anyInt(), any(), any());
    }

    @Test
    public void nonSelectedSourceCannotAdoptCreatedContents() {
        when(mSelector.getCurrentTab()).thenReturn(mock(Tab.class));
        mGate.record(/* tabId= */ 7, /* occurredAtMillis= */ 990);

        assertFalse(adopt(WindowOpenDisposition.NEW_FOREGROUND_TAB, true));
        verify(mCreator, never())
                .createTabWithWebContents(any(), anyBoolean(), any(), anyInt(), any(), any());
    }

    @Test
    public void missingRendererGestureAndUnsupportedDispositionAreBlocked() {
        mNavigation.recordPageGesture(/* occurredAtMillis= */ 990);
        assertFalse(adopt(WindowOpenDisposition.NEW_FOREGROUND_TAB, false));

        mNavigation.recordPageGesture(/* occurredAtMillis= */ 990);
        assertFalse(adopt(WindowOpenDisposition.NEW_PICTURE_IN_PICTURE, true));
        assertFalse(adopt(WindowOpenDisposition.NEW_FOREGROUND_TAB, true));
    }

    @Test
    public void pictureInPictureNeverFallsThroughCreatedPageRoute() {
        mNavigation.recordPageGesture(/* occurredAtMillis= */ 990);
        TaffyTabDelegateFactory.TaffyTabWebContentsDelegate delegate =
                (TaffyTabDelegateFactory.TaffyTabWebContentsDelegate)
                        new TaffyTabDelegateFactory(mNavigation)
                                .createWebContentsDelegate(mSourceTab);

        assertFalse(
                delegate.addNewContents(
                        mSourceContents,
                        mCreatedContents,
                        TaffyTestGurl.from("https://example.test/"),
                        WindowOpenDisposition.NEW_PICTURE_IN_PICTURE,
                        /* windowFeatures= */ null,
                        /* userGesture= */ true,
                        new PictureInPictureWindowOptions()));
    }

    @Test
    public void contextMenuOpensSafeLinkInSameProfileBackgroundModel() {
        when(mCreator.createNewTab(
                        any(LoadUrlParams.class),
                        eq(TabLaunchType.FROM_LONGPRESS_BACKGROUND),
                        same(mSourceTab)))
                .thenReturn(mCreatedTab);

        assertTrue(
                mNavigation.openContextLink(
                        mSourceTab, TaffyTestGurl.from("https://example.test/path")));
        assertFalse(
                mNavigation.openContextLink(
                        mSourceTab, TaffyTestGurl.from("https://user:secret@example.test/")));
    }

    @Test
    public void closeCancelsPopupAndContextMenuWork() {
        mNavigation.recordPageGesture(/* occurredAtMillis= */ 990);
        mNavigation.close();

        assertFalse(adopt(WindowOpenDisposition.NEW_FOREGROUND_TAB, true));
        assertFalse(
                mNavigation.openContextLink(
                        mSourceTab, TaffyTestGurl.from("https://example.test/")));
    }

    private boolean adopt(int disposition, boolean rendererUserGesture) {
        return mNavigation.adoptCreatedPage(
                mSourceTab,
                mSourceContents,
                mCreatedContents,
                TaffyTestGurl.from("https://example.test/"),
                disposition,
                rendererUserGesture);
    }
}

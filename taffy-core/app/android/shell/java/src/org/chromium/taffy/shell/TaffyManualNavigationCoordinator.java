// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.shell;

import android.app.Activity;

import androidx.annotation.Nullable;
import androidx.annotation.VisibleForTesting;

import org.chromium.chrome.browser.tab.Tab;
import org.chromium.chrome.browser.tab.TabLaunchType;
import org.chromium.chrome.browser.tabmodel.TabCreator;
import org.chromium.chrome.browser.tabmodel.TabModelSelector;
import org.chromium.components.external_intents.ExternalNavigationParams;
import org.chromium.content_public.browser.LoadUrlParams;
import org.chromium.content_public.browser.WebContents;
import org.chromium.ui.mojom.WindowOpenDisposition;
import org.chromium.url.GURL;

import java.util.concurrent.CompletableFuture;
import java.util.function.Supplier;

/** Owns all manual page exits from one Android browser window. */
final class TaffyManualNavigationCoordinator {
    private final Supplier<TabModelSelector> mSelectorSupplier;
    private final TaffyManualGestureGate mGestureGate;
    private final TaffyExternalAppHandoff mExternalHandoff;
    private boolean mClosed;

    TaffyManualNavigationCoordinator(
            Activity activity, Supplier<TabModelSelector> selectorSupplier) {
        this(selectorSupplier, new TaffyManualGestureGate(), activity);
    }

    private TaffyManualNavigationCoordinator(
            Supplier<TabModelSelector> selectorSupplier,
            TaffyManualGestureGate gestureGate,
            Activity activity) {
        this(selectorSupplier, gestureGate, new TaffyExternalAppHandoff(activity, gestureGate));
    }

    @VisibleForTesting
    TaffyManualNavigationCoordinator(
            Supplier<TabModelSelector> selectorSupplier,
            TaffyManualGestureGate gestureGate,
            TaffyExternalAppHandoff externalHandoff) {
        mSelectorSupplier = selectorSupplier;
        mGestureGate = gestureGate;
        mExternalHandoff = externalHandoff;
    }

    void recordPageGesture(long occurredAtMillis) {
        TabModelSelector selector = currentSelector();
        Tab tab = selector == null ? null : selector.getCurrentTab();
        if (tab != null && !tab.isDestroyed() && !tab.isClosing()) {
            mGestureGate.record(tab.getId(), occurredAtMillis);
        }
    }

    void clearPageGesture() {
        mGestureGate.clear();
    }

    boolean openExternalApp(Tab sourceTab, @Nullable ExternalNavigationParams params) {
        return isCurrent(sourceTab) && mExternalHandoff.open(sourceTab, params);
    }

    boolean adoptCreatedPage(
            Tab sourceTab,
            WebContents sourceContents,
            WebContents createdContents,
            GURL targetUrl,
            int disposition,
            boolean rendererUserGesture) {
        if (mClosed
                || !rendererUserGesture
                || !isCurrent(sourceTab)
                || sourceTab.getWebContents() != sourceContents
                || sourceContents == createdContents
                || createdContents.isDestroyed()) {
            return false;
        }
        if (!mGestureGate.consume(sourceTab.getId())) return false;
        @TabLaunchType int launchType = launchTypeFor(disposition);
        if (launchType == TabLaunchType.UNSET) return false;
        TabCreator creator = creatorFor(sourceTab);
        if (creator == null) return false;
        Tab createdTab =
                creator.createTabWithWebContents(
                        sourceTab,
                        /* shouldPin= */ false,
                        createdContents,
                        launchType,
                        targetUrl,
                        CompletableFuture.completedFuture(true));
        return createdTab != null;
    }

    boolean openContextLink(Tab sourceTab, GURL address) {
        if (mClosed || !isCurrent(sourceTab) || !isSafeWebAddress(address)) return false;
        TabCreator creator = creatorFor(sourceTab);
        if (creator == null) return false;
        return creator.createNewTab(
                        new LoadUrlParams(address.getSpec()),
                        TabLaunchType.FROM_LONGPRESS_BACKGROUND,
                        sourceTab)
                != null;
    }

    void close() {
        if (mClosed) return;
        mClosed = true;
        mGestureGate.close();
        mExternalHandoff.close();
    }

    @VisibleForTesting
    static boolean isSafeWebAddress(GURL address) {
        return address.isValid()
                && ("http".equals(address.getScheme()) || "https".equals(address.getScheme()))
                && address.getUsername().isEmpty()
                && address.getPassword().isEmpty();
    }

    private boolean isCurrent(Tab tab) {
        TabModelSelector selector = currentSelector();
        return !mClosed
                && selector != null
                && selector.getCurrentTab() == tab
                && !tab.isDestroyed()
                && !tab.isClosing();
    }

    private @Nullable TabCreator creatorFor(Tab sourceTab) {
        TabModelSelector selector = currentSelector();
        return selector == null
                ? null
                : selector.getTabCreatorManager().getTabCreator(sourceTab.isOffTheRecord());
    }

    private @Nullable TabModelSelector currentSelector() {
        return mClosed ? null : mSelectorSupplier.get();
    }

    private static @TabLaunchType int launchTypeFor(int disposition) {
        return switch (disposition) {
            case WindowOpenDisposition.NEW_FOREGROUND_TAB -> TabLaunchType.FROM_LINK;
            case WindowOpenDisposition.NEW_BACKGROUND_TAB ->
                    TabLaunchType.FROM_LONGPRESS_BACKGROUND;
            case WindowOpenDisposition.NEW_POPUP, WindowOpenDisposition.NEW_WINDOW ->
                    TabLaunchType.FROM_LINK_CREATING_NEW_WINDOW;
            default -> TabLaunchType.UNSET;
        };
    }
}

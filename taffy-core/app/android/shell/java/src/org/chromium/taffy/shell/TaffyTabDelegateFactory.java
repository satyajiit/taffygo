// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.shell;

import androidx.annotation.Nullable;
import androidx.annotation.VisibleForTesting;

import org.chromium.chrome.browser.pdf.PdfInfo;
import org.chromium.chrome.browser.tab.Tab;
import org.chromium.chrome.browser.tab.TabDelegateFactory;
import org.chromium.chrome.browser.tab.TabWebContentsDelegateAndroid;
import org.chromium.chrome.browser.ui.native_page.NativePage;
import org.chromium.chrome.browser.util.PictureInPictureWindowOptions;
import org.chromium.chrome.browser.util.WindowFeatures;
import org.chromium.components.browser_ui.util.BrowserControlsVisibilityDelegate;
import org.chromium.components.embedder_support.contextmenu.ContextMenuPopulatorFactory;
import org.chromium.components.external_intents.ExternalNavigationHandler;
import org.chromium.content_public.browser.WebContents;
import org.chromium.url.GURL;

/** Supplies one tab with TaffyGo's bounded manual-navigation adapters. */
public class TaffyTabDelegateFactory implements TabDelegateFactory {
    private final @Nullable TaffyManualNavigationCoordinator mManualNavigation;

    @VisibleForTesting
    TaffyTabDelegateFactory() {
        this(null);
    }

    TaffyTabDelegateFactory(TaffyManualNavigationCoordinator manualNavigation) {
        mManualNavigation = manualNavigation;
    }

    @Override
    public TabWebContentsDelegateAndroid createWebContentsDelegate(Tab tab) {
        return new TaffyTabWebContentsDelegate(tab, mManualNavigation);
    }

    /**
     * Always returns the tab-scoped handler, even when this factory is deliberately unconfigured.
     *
     * <p>The configured handler routes only the bounded manual chooser path. The test-only
     * unconfigured handler fails closed. Both are real objects because upstream must pair the
     * handler with its {@code WebContentsObserver}; returning null abandons a live tab half-wired.
     *
     * <p>Never null. A null here is what put {@code InterceptNavigationDelegateImpl} into the
     * half-wired state this fix exists to remove, and the declared type says so.
     */
    @Override
    public ExternalNavigationHandler createExternalNavigationHandler(Tab tab) {
        return new TaffyExternalNavigationHandler(tab, mManualNavigation);
    }

    @Override
    public @Nullable ContextMenuPopulatorFactory createContextMenuPopulatorFactory(Tab tab) {
        return mManualNavigation == null
                ? null
                : new TaffyContextMenuPopulatorFactory(tab, mManualNavigation);
    }

    @Override
    public @Nullable BrowserControlsVisibilityDelegate createBrowserControlsVisibilityDelegate(
            Tab tab) {
        return null;
    }

    @Override
    public @Nullable NativePage createNativePage(
            String url, @Nullable NativePage candidatePage, Tab tab, @Nullable PdfInfo pdfInfo) {
        return null;
    }

    /** Adopts only newly created pages backed by a fresh physical gesture on this exact tab. */
    @VisibleForTesting
    static class TaffyTabWebContentsDelegate extends TabWebContentsDelegateAndroid {
        private final Tab mTab;
        private final @Nullable TaffyManualNavigationCoordinator mManualNavigation;
        private boolean mDestroyed;

        TaffyTabWebContentsDelegate(
                Tab tab, @Nullable TaffyManualNavigationCoordinator manualNavigation) {
            mTab = tab;
            mManualNavigation = manualNavigation;
        }

        @Override
        public boolean shouldResumeRequestsForCreatedWindow() {
            return true;
        }

        @Override
        public boolean addNewContents(
                WebContents sourceWebContents,
                WebContents webContents,
                GURL targetUrl,
                int disposition,
                WindowFeatures windowFeatures,
                boolean userGesture,
                @Nullable PictureInPictureWindowOptions pictureInPictureWindowOptions) {
            return !mDestroyed
                    && pictureInPictureWindowOptions == null
                    && mManualNavigation != null
                    && mManualNavigation.adoptCreatedPage(
                            mTab,
                            sourceWebContents,
                            webContents,
                            targetUrl,
                            disposition,
                            userGesture);
        }

        @Override
        public void setOverlayMode(boolean useOverlayMode) {}

        @Override
        public void destroy() {
            mDestroyed = true;
        }
    }
}

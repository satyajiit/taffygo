// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.shell;

import android.content.Context;

import org.chromium.chrome.browser.tab.Tab;
import org.chromium.components.embedder_support.contextmenu.ContextMenuNativeDelegate;
import org.chromium.components.embedder_support.contextmenu.ContextMenuParams;
import org.chromium.components.embedder_support.contextmenu.ContextMenuPopulator;
import org.chromium.components.embedder_support.contextmenu.ContextMenuPopulatorFactory;

/** Creates the small manual-only context menu owned by a TaffyGo tab. */
final class TaffyContextMenuPopulatorFactory implements ContextMenuPopulatorFactory {
    private final Tab mTab;
    private final TaffyManualNavigationCoordinator mNavigation;
    private boolean mDestroyed;

    TaffyContextMenuPopulatorFactory(Tab tab, TaffyManualNavigationCoordinator navigation) {
        mTab = tab;
        mNavigation = navigation;
    }

    @Override
    public ContextMenuPopulator createContextMenuPopulator(
            Context context, ContextMenuParams params, ContextMenuNativeDelegate nativeDelegate) {
        return new TaffyContextMenuPopulator(context, mTab, params, mNavigation, mDestroyed);
    }

    @Override
    public boolean isEnabled() {
        return !mDestroyed && !mTab.isDestroyed() && !mTab.isClosing();
    }

    @Override
    public void onDestroy() {
        mDestroyed = true;
    }
}

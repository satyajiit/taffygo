// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.shell;

import androidx.annotation.Nullable;

import org.chromium.chrome.browser.tab.Tab;
import org.chromium.components.external_intents.ExternalNavigationHandler;
import org.chromium.components.external_intents.ExternalNavigationParams;

/** Routes only a physically attested manual external-protocol navigation to the system chooser. */
public class TaffyExternalNavigationHandler extends ExternalNavigationHandler {

    private final Tab mTab;
    private final TaffyExternalNavigationDelegate mTaffyDelegate;
    private final @Nullable TaffyManualNavigationCoordinator mManualNavigation;

    /**
     * @param tab the tab whose navigations this handler answers for. Its {@code WindowAndroid} must
     *     already be set: upstream's constructor dereferences it, and every TaffyGo tab is built
     *     with a window before its {@code WebContents} exists ({@code TabBuilder.setWindow}), which
     *     is strictly before this is first constructed.
     */
    public TaffyExternalNavigationHandler(Tab tab) {
        this(tab, null);
    }

    TaffyExternalNavigationHandler(
            Tab tab, @Nullable TaffyManualNavigationCoordinator manualNavigation) {
        this(tab, new TaffyExternalNavigationDelegate(tab), manualNavigation);
    }

    /**
     * The real constructor, kept separate only because {@code super(...)} must be the first
     * statement and the delegate has to be remembered as well as passed.
     */
    private TaffyExternalNavigationHandler(
            Tab tab,
            TaffyExternalNavigationDelegate delegate,
            @Nullable TaffyManualNavigationCoordinator manualNavigation) {
        super(delegate);
        mTab = tab;
        mTaffyDelegate = delegate;
        mManualNavigation = manualNavigation;
    }

    @Override
    public OverrideUrlLoadingResult shouldOverrideUrlLoading(ExternalNavigationParams params) {
        if (mManualNavigation != null && mManualNavigation.openExternalApp(mTab, params)) {
            return OverrideUrlLoadingResult.forExternalIntent();
        }
        // InterceptNavigationDelegateImpl continues accepted browser schemes for this result, but
        // blocks an external protocol and logs it to the page console. A missing/cancelled chooser
        // therefore never becomes a hidden browser or fallback navigation.
        return OverrideUrlLoadingResult.forNoOverride();
    }

    /** The policy this handler was built with. The shell's own tests read it; nothing else may. */
    TaffyExternalNavigationDelegate getDelegateForTesting() {
        return mTaffyDelegate;
    }
}

// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.shell;

import android.app.Activity;
import android.util.Pair;

import org.chromium.base.ContextUtils;
import org.chromium.base.lifetime.Destroyable;
import org.chromium.base.supplier.OneshotSupplier;
import org.chromium.chrome.browser.app.tabmodel.AsyncTabParamsManagerSingleton;
import org.chromium.chrome.browser.app.tabmodel.HeadlessBrowserControlsStateProvider;
import org.chromium.chrome.browser.app.tabmodel.TabPersistentStoreFactory;
import org.chromium.chrome.browser.app.tabwindow.TabWindowManagerSingleton;
import org.chromium.chrome.browser.crypto.CipherFactory;
import org.chromium.chrome.browser.profiles.ProfileProvider;
import org.chromium.chrome.browser.tab_ui.TabContentManager;
import org.chromium.chrome.browser.tabmodel.AsyncTabParamsManager;
import org.chromium.chrome.browser.tabmodel.NextTabPolicy;
import org.chromium.chrome.browser.tabmodel.RecordingTabCreatorManager;
import org.chromium.chrome.browser.tabmodel.SupportedProfileType;
import org.chromium.chrome.browser.tabmodel.TabCreator;
import org.chromium.chrome.browser.tabmodel.TabCreatorManager;
import org.chromium.chrome.browser.tabmodel.TabModelSelector;
import org.chromium.chrome.browser.tabmodel.TabModelSelectorBase;
import org.chromium.chrome.browser.tabmodel.TabPersistencePolicy;
import org.chromium.chrome.browser.tabmodel.TabPersistentStore;
import org.chromium.chrome.browser.tabmodel.TabPersistentStore.TabPersistentStoreObserver;
import org.chromium.chrome.browser.tabmodel.TabPersistentStoreImpl;
import org.chromium.chrome.browser.tabmodel.TabbedModeTabPersistencePolicy;
import org.chromium.chrome.browser.tabwindow.TabWindowManager;
import org.chromium.ui.base.WindowAndroid;

/**
 * TaffyGo's tabs: the selector, the creators, the persistent store and the
 * thumbnail cache.
 *
 * <p><b>This extends nothing, and that is upstream's own instruction.</b>
 * {@code TabModelOrchestrator}'s constructor is annotated
 * {@code @VisibleForTesting(otherwise = PROTECTED)}, so a downstream subclass
 * of it is a lint failure rather than a design. The in-tree precedent for
 * standing the same pieces up directly is
 * {@code HeadlessTabModelOrchestrator}, which is also not an orchestrator and
 * says so in its own class comment ("does not currently share any interface ...
 * as its lifecycle is substantially different"). The sequence below is that
 * class's, corrected against
 * {@code CustomTabsTabModelOrchestrator} wherever the headless answer was
 * headless-specific.
 *
 * <p><b>The construction order is not rearrangeable, and each step is
 * load-bearing:</b>
 *
 * <ol>
 *   <li>the two creators are built before the selector, because the selector's
 *       {@code TabCreatorManager} is a constructor argument;
 *   <li>the manager assigns the selector and the window's persistence id as one
 *       operation;
 *   <li>the selector is built with the <i>supplier</i> of the profile, never a
 *       profile, because
 *       {@code onNativeLibraryReady} is where it asserts one has arrived;
 *   <li>the persistence policy and store use the assigned id rather than the
 *       requested one;
 *   <li>the store observes the selector, and its {@code onStateLoaded} is the
 *       one thing that makes
 *       {@code isTabStateInitialized()} true — without it every surface waits
 *       forever for a session that has already been read;
 *   <li>{@code onNativeLibraryReady} creates the tab models, which is when the
 *       creators are given their model and their order controller;
 *   <li>{@code loadState} reads the metadata file and {@code restoreTabs} turns
 *       it into tabs.
 * </ol>
 *
 * <p><b>The selector is assigned by {@code TabWindowManager} before persistence
 * is built.</b> The manager may return a different id when another Activity
 * owns the requested one. That assigned id is the persistence identity: policy
 * index, store tag, and window lookup all use the same value. This is what lets
 * two product Activities remain separate and lets Chromium reparent a tab
 * without making tab-state cleanup guess which selectors exist.
 *
 * <p><b>What a tab in this model does not get, and the part of it that was not
 * harmless.</b>
 * {@code TabImpl.getActivity()} returns a {@code ChromeActivity} or null, and
 * {@code TaffyBrowserActivity} is not one — so {@code isCustomTab()}, {@code
 * isTabInPWA()} and
 * {@code isTabInBrowser()} all answer false and the lookup logs one line saying
 * the activity is not a {@code ChromeActivity}. Three of those four are
 * genuinely upstream's own degraded path for a host it does not know. The
 * fourth was a crash. {@code TabImpl.loadIfNeeded} guards on the same lookup,
 * so a tab restored frozen by upstream's {@code createFrozenTab} was never
 * inflated at all, and every second launch of the browser died on it. {@link
 * TaffyTabCreator} is where that is answered — a TaffyGo tab holds its {@code
 * WebContents} before it enters this model — and the reasoning, including why
 * relaxing the guard is not one of the options, is written down there.
 *
 * <p>What remains true is the choice underneath: the alternative to all of this
 * is extending
 * {@code ChromeActivity}, which is the interface decision 0024 declines.
 */
public class TaffyTabModelHolder implements Destroyable {
    /**
     * The key tab state is encrypted with, for as long as the process lives.
     *
     * <p>One per process rather than one per holder, following
     * {@code HeadlessTabModelOrchestrator}: the key belongs to the profile's data
     * on disk, and two factories would be two keys writing one directory. It is
     * deliberately <i>not</i> saved into the activity's instance state, which is
     * what {@code CipherFactory.saveToBundle} is for — so the key does not
     * survive the process, which is why {@link #TaffyTabModelHolder} declines to
     * read incognito files back.
     */
    private static final CipherFactory CIPHER_FACTORY = new CipherFactory();

    private final TabModelSelectorBase mSelector;
    private final int mWindowId;
    private TabPersistentStore mStore;
    private final TabContentManager mTabContentManager;
    private final TabCreatorManager mTabCreatorManager;
    private final TaffyManualNavigationCoordinator mManualNavigation;

    /**
     * Builds every piece and starts the session restore.
     *
     * @param activity the activity the tabs belong to; upstream reads its context
     *     and its task id.
     * @param window the root window every tab's {@code WebContents} is bound to.
     * @param profileProviderSupplier the activity's provider. <b>It must already
     *     be fulfilled.</b>
     *     {@code TabModelSelectorImpl.onNativeLibraryReady} calls {@code get()}
     * on it and asserts the result is non-null, so the caller waits through
     *     {@code OneshotSupplier.runSyncOrOnAvailable} rather than guessing
     * whether the profile manager happens to be up by {@code
     * finishNativeInitialization()}.
     * @param startPageUrl the address a tab opens on when nothing else says
     *     otherwise. It reaches the creators because a saved tab whose navigation
     *     history cannot be read back still has to land somewhere, and TaffyGo
     *     resolves no new-tab page for it to land on — see
     *     {@link TaffyTabRestore#fallbackUrlFor}.
     * @param requestedWindowId the restored persistence id, or zero for a new
     *     Activity. Chromium may assign another free id and that returned id
     *     becomes authoritative.
     */
    public TaffyTabModelHolder(Activity activity, WindowAndroid window,
            OneshotSupplier<ProfileProvider> profileProviderSupplier, String startPageUrl,
            int requestedWindowId) {
        AsyncTabParamsManager asyncTabParamsManager = AsyncTabParamsManagerSingleton.getInstance();
        mManualNavigation =
                new TaffyManualNavigationCoordinator(activity, this::getTabModelSelector);

        // The creators need the selector and the selector needs the creators. The
        // cycle is broken the way upstream breaks it: the creator holds a supplier,
        // and reads it only when it already has a tab model, which is strictly
        // after the selector exists.
        TabCreator regularTabCreator =
                new TaffyTabCreator(activity, window, profileProviderSupplier,
                        /* incognito= */ false, asyncTabParamsManager, this::getTabModelSelector,
                        startPageUrl, mManualNavigation);
        TabCreator incognitoTabCreator =
                new TaffyTabCreator(activity, window, profileProviderSupplier,
                        /* incognito= */ true, asyncTabParamsManager, this::getTabModelSelector,
                        startPageUrl, mManualNavigation);
        mTabCreatorManager = incognito -> incognito ? incognitoTabCreator : regularTabCreator;

        TabWindowManager tabWindowManager = TabWindowManagerSingleton.getInstance();
        Pair<Integer, TabModelSelector> selectorAssignment = tabWindowManager.requestSelector(
                activity,
                // No modal dialog manager. TaffyGo's dialogs are its own screens
                // (SCR-2xx) and are not transferred yet; upstream reads null as
                // "close tab groups without asking", which is the behaviour of a
                // browser with no group UI.
                /* modalDialogManager= */ null, profileProviderSupplier, mTabCreatorManager,
                ()
                        -> NextTabPolicy.LOCATIONAL,
                new TaffyMismatchedIndicesHandler(), requestedWindowId, SupportedProfileType.MIXED);
        if (selectorAssignment == null) {
            throw new TaffyTabWindowUnavailableException();
        }
        if (!(selectorAssignment.second instanceof TabModelSelectorBase selector)) {
            throw new IllegalStateException(
                    "Chromium returned a selector without tabbed lifecycle support");
        }
        mWindowId = selectorAssignment.first;
        mSelector = selector;
        mSelector.selectModel(/* incognito= */ false);

        TabPersistencePolicy policy = new TabbedModeTabPersistencePolicy(mWindowId,
                /* mergeTabsOnStartup= */ false,
                /* tabMergingEnabled= */ false,
                /* isRecreatingSupplier= */ () -> false);
        mStore = TabPersistentStoreFactory.buildAuthoritativeStore(
                TabPersistentStoreImpl.CLIENT_TAG_REGULAR,
                // Null selects upstream's own default migration manager for this window
                // tag, which is what CustomTabsTabModelOrchestrator passes. The
                // headless orchestrator supplies its own only because it also runs a
                // shadow store.
                /* migrationManager= */ null, policy, mSelector,
                // Wrapping the creator manager is not optional: the store records which
                // tabs it created during a restore, and that record is how upstream
                // tells a restored tab from a new one.
                new RecordingTabCreatorManager(mTabCreatorManager), tabWindowManager,
                String.valueOf(mWindowId), CIPHER_FACTORY,
                /* recordLegacyTabCountMetrics= */ true,
                /* isFromRecreating= */ false);

        // No shadow store. Upstream runs one to validate a new storage backend
        // against the old one; TaffyGo has no opinion about that migration and
        // running a second writer over the same directory would be adopting one.
        mStore.addObserver(new TabPersistentStoreObserver() {
            @Override
            public void onStateLoaded() {
                mSelector.markTabStateInitialized();
                mStore.removeObserver(this);
            }
        });

        mTabContentManager = new TabContentManager(ContextUtils.getApplicationContext(),
                // No browser controls exist in this product: TaffyGo's chrome is
                // Compose above the page, not a retractable toolbar the compositor
                // offsets for. The headless provider is upstream's own all-zeroes
                // implementation of exactly that.
                new HeadlessBrowserControlsStateProvider(),
                // Snapshots for the tab switcher. The cards read these local
                // bitmaps; chrome never fetches a vendor icon from the network.
                /* snapshotsEnabled= */ true, mSelector::getTabById, tabWindowManager);
        mTabContentManager.initWithNative();

        mSelector.onNativeLibraryReady(mTabContentManager);
        policy.setTabContentManager(mTabContentManager);

        mStore.onNativeLibraryReady();
        mStore.loadState(
                // Private tabs do not survive the process, because the key that
                // encrypted them does not: CIPHER_FACTORY is never written to the
                // instance-state bundle, so a new process cannot decrypt the previous
                // one's files and would fail tab by tab. This says so up front instead
                // of attempting a restore that cannot succeed.
                /* ignoreIncognitoFiles= */ true,
                /* ignoreRegularFiles= */ false);
        mStore.restoreTabs(/* setActiveTab= */ true);
    }

    /**
     * The selector every surface reads through. Never null once this object
     * exists.
     */
    public TabModelSelector getTabModelSelector() {
        return mSelector;
    }

    /** Chromium-assigned id used by both the manager and the persistent store. */
    public int getWindowId() {
        return mWindowId;
    }

    /** How a new tab is made, regular or private. */
    public TabCreatorManager getTabCreatorManager() {
        return mTabCreatorManager;
    }

    /** The snapshot cache the tab switcher reads for card previews. */
    public TabContentManager getTabContentManager() {
        return mTabContentManager;
    }

    /** Records a physical pointer release already bounded to the visible page by the Activity. */
    void recordManualPageGesture(long occurredAtMillis) {
        mManualNavigation.recordPageGesture(occurredAtMillis);
    }

    /** Revokes the transient proof when this window stops being person-visible. */
    void clearManualPageGesture() {
        mManualNavigation.clearPageGesture();
    }

    /**
     * Writes the tab list to disk now, rather than when the store next gets round
     * to it.
     *
     * <p>Called from {@code onStopWithNative()}, which is where upstream's tabbed
     * activity calls it and the only moment it earns its cost: the store already
     * writes the list asynchronously whenever the model changes, so this exists
     * for the case those writes have not landed and the process is about to be a
     * candidate for the low-memory killer. It is not called from
     * {@link #destroy()}, because {@code onDestroy} is exactly the callback a
     * killed process does not get — a synchronous disk write there would pay for
     * a guarantee it cannot make.
     */
    public void saveState() {
        TabPersistentStore store = mStore;
        if (store != null) store.saveState();
    }

    /**
     * Releases the store before {@code TabWindowManager} transfers this id during
     * Activity recreation. The selector and tabs remain alive until normal
     * Activity teardown.
     */
    public void releasePersistentStoreForWindowReassignment() {
        TabPersistentStore store = mStore;
        if (store == null) return;
        store.saveState();
        store.destroy();
        mStore = null;
    }

    /**
     * Releases everything, in the order the ownership runs.
     *
     * <p>The store first, because it observes the selector and its teardown reads
     * tabs the selector still owns; the selector second, which destroys every
     * tab; the thumbnail cache last, because its native half is referenced by the
     * tab models the selector just tore down.
     */
    @Override
    public void destroy() {
        mManualNavigation.close();
        TabPersistentStore store = mStore;
        if (store != null) {
            store.destroy();
            mStore = null;
        }
        mSelector.destroy();
        mTabContentManager.destroy();
    }
}

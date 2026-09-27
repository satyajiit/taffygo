// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.shell;

import android.app.Activity;
import android.content.Intent;

import androidx.annotation.Nullable;

import org.chromium.base.Callback;
import org.chromium.base.supplier.OneshotSupplier;
import org.chromium.chrome.browser.compositor.CompositorViewHolder;
import org.chromium.chrome.browser.profiles.Profile;
import org.chromium.chrome.browser.profiles.ProfileProvider;
import org.chromium.chrome.browser.tab.EmptyTabObserver;
import org.chromium.chrome.browser.tab.Tab;
import org.chromium.chrome.browser.tab.TabBuilder;
import org.chromium.chrome.browser.tab.TabCreationState;
import org.chromium.chrome.browser.tab.TabLaunchType;
import org.chromium.chrome.browser.tab.TabState;
import org.chromium.chrome.browser.tab.WebContentsState;
import org.chromium.chrome.browser.tabmodel.AsyncTabParamsManager;
import org.chromium.chrome.browser.tabmodel.ChromeTabCreator;
import org.chromium.chrome.browser.tabmodel.TabModel;
import org.chromium.chrome.browser.tabmodel.TabModelSelector;
import org.chromium.content_public.browser.LoadUrlParams;
import org.chromium.content_public.browser.WebContents;
import org.chromium.ui.base.PageTransition;
import org.chromium.ui.base.WindowAndroid;
import org.chromium.url.GURL;

import java.util.function.Predicate;
import java.util.function.Supplier;

/**
 * How a TaffyGo tab comes into existence: upstream's own creator, with no compositor behind it and
 * with one method replaced, because a tab in this activity has to be alive before it is anything
 * else.
 *
 * <p><b>Why {@code ChromeTabCreator} rather than a hand-written {@code TabCreator}.</b> The
 * interface is four methods and each one is easy to write badly. This subclass inherits {@code
 * createNewTab}, {@code createTabWithWebContents} and the id assignment, ordering, transition types
 * and URL fixup that go with them. Writing those again would be writing a browser that has to be
 * debugged separately from the one Chrome ships.
 *
 * <p><b>THE ONE METHOD THAT IS NOT INHERITED, AND WHY.</b> {@code TabImpl.getActivity()} answers
 * with a {@code ChromeActivity} or null, and {@link TaffyBrowserActivity} deliberately is not one —
 * not extending {@code ChromeActivity} is the whole reason owning every screen costs no upstream
 * patch. Upstream's {@code createFrozenTab} builds a tab holding a frozen {@code WebContentsState},
 * to be inflated on first show. In this activity that tab is never inflated at all:
 *
 * <ul>
 *   <li>{@code TabImpl.loadIfNeeded} opens with {@code if (getActivity(true) == null) return
 *       false}, so the load path stops at the door and logs one line saying the activity is not a
 *       {@code ChromeActivity};
 *   <li>the only un-freezer behind that door, {@code TabImpl.unfreezeContents}, is {@code private}
 *       and does {@code assumeNonNull(getActivity()).getCompositorViewHolderSupplier()} — and
 *       {@code assumeNonNull} is a runtime no-op ({@code build/android/java/org/chromium/build/
 *       NullUtil.java}), so relaxing the guard would replace a silent bail with a {@code
 *       NullPointerException} rather than with a loaded page.
 * </ul>
 *
 * <p>Measured, not inferred: every launch after the first restored its session, every restored tab
 * stayed frozen, and the browser then died in the page host, which had been handed a tab with no
 * {@code WebContents}. So no caller outside {@code TabImpl} can un-freeze a tab here, and the only
 * reachable answer is to never hand {@code TabImpl} a frozen one. {@link #createFrozenTab} restores
 * the {@code WebContents} first and builds a tab that already owns it.
 *
 * <p><b>The invariant this class exists to hold is "live before it enters the model", not "live
 * before it is shown".</b> {@code TabHelpers.initTabHelpers} attaches {@code ReaderModeManager} at
 * tab creation while {@code TabDistillabilityProvider} is only created by {@code
 * TabImpl.initWebContents}, so a tab shown before it has a {@code WebContents} takes an NPE inside
 * {@code ReaderModeManager.onShown} — which {@code TabPersistentStoreImpl} catches and logs, losing
 * the rest of the session silently. Building the {@code WebContents} inside {@code
 * TabBuilder.build()} puts it strictly before {@code TabModel.addTab}, and therefore strictly
 * before {@code TabModelSelectorImpl.requestToShowTab}. Restoring it after the tab is in the model
 * would bring that crash straight back.
 *
 * <p><b>What a restored tab costs here that it does not cost upstream.</b> Two, both deliberate and
 * both registered as OD-096. Every restored tab now holds a browser-side {@code WebContents} from
 * process start rather than from first show — {@code noRenderer} keeps a renderer process out of
 * it, so the cost is navigation entries rather than processes, and it is still unmeasured. And two
 * persisted fields are dropped because {@code TabState} cannot be applied through {@code
 * TabBuilder.setTabState} here (see {@link #createFrozenTab}): the launch type a tab was originally
 * created with, and the timestamp of its last committed navigation. Nothing in this build reads
 * either, the second self-heals on the next commit, and both are named rather than quietly lost.
 *
 * <p><b>The one thing this class already existed to say is that there is no {@code
 * CompositorViewHolder}.</b> {@code ChromeTabCreator} dereferences its supplier only on reparenting
 * paths, each passing the result into {@code
 * ReparentingDelegateFactory.createReparentingTaskDelegate}, whose first parameter is declared
 * {@code final @Nullable CompositorViewHolder}. So null is a value that parameter is declared to
 * take, and TaffyGo has no such view: its page host is {@code TaffyPageHostView}, built on {@code
 * ContentViewRenderView}, and adopting {@code CompositorViewHolder} would drag in Chrome's layout
 * manager, browser-controls manager and tab-content compositor, which is the whole of what decision
 * 0024 says this product does not do. TaffyGo does not reparent tabs at M1, so whether {@code
 * ReparentingTask.finish} tolerates such a delegate is not established here; the day something
 * reparents, that is the question to answer first.
 *
 * <p>{@code mActivity} is read once by upstream, for {@code getPackageName()} in {@code
 * launchUrlFromExternalApp}, which nothing here calls.
 */
public class TaffyTabCreator extends ChromeTabCreator {

    /**
     * There is no compositor view holder, and there will not be one.
     *
     * <p>A named constant rather than an inline lambda so that the reason lives with the value: see
     * this class's own documentation. It is typed rather than raw because {@code javac} has to see
     * the target type to accept {@code null}.
     */
    private static final Supplier<CompositorViewHolder> NO_COMPOSITOR_VIEW_HOLDER = () -> null;

    private final WindowAndroid mWindow;
    private final OneshotSupplier<ProfileProvider> mProfileProviderSupplier;
    private final boolean mIsIncognito;
    private final Supplier<TabModelSelector> mTabModelSelectorSupplier;
    private final String mStartPageUrl;
    private final TaffyManualNavigationCoordinator mManualNavigation;

    /**
     * This creator's model, kept because upstream's copy is private.
     *
     * <p>{@code ChromeTabCreator} holds {@code mTabModel} privately and exposes only the public
     * setter {@link #setTabModel} that {@code TabModelSelectorImpl.onNativeLibraryReady} calls. So
     * the way to read it from a subclass is to observe it being written, which is what the override
     * below does — and it calls {@code super} first, because upstream's copy is what every
     * inherited method uses.
     *
     * <p>Null until the tab models are created, which is the same "not ready yet" that upstream's
     * own {@code createFrozenTab} answers null for.
     */
    private @Nullable TabModel mTabModel;

    /**
     * @param activity the activity the created tabs belong to.
     * @param window the root window every tab's {@code WebContents} is bound to.
     * @param profileProviderSupplier the activity's profile provider; upstream resolves the profile
     *     through it on every creation, so it is passed rather than a {@code Profile}.
     * @param incognito whether this creator makes off-the-record tabs. One creator per model, which
     *     is upstream's arrangement and the reason {@code TabCreatorManager} takes a boolean.
     * @param asyncTabParamsManager the process-wide pending-tab table; a tab arriving by intent is
     *     matched to its parameters through it.
     * @param tabModelSelectorSupplier reads back the selector this creator's tabs are added to. It
     *     is a supplier rather than the object because the selector is constructed <i>from</i> the
     *     creators, so one of the two edges has to be late.
     * @param startPageUrl the address a tab opens on when nothing else says otherwise. Needed here
     *     because a session that cannot be read back has to land somewhere, and TaffyGo resolves no
     *     new-tab page to land on — see {@link TaffyTabRestore#fallbackUrlFor}.
     * @param manualNavigation the one window-owned route for context-menu, popup and external-app
     *     actions initiated by a person.
     */
    public TaffyTabCreator(
            Activity activity,
            WindowAndroid window,
            OneshotSupplier<ProfileProvider> profileProviderSupplier,
            boolean incognito,
            AsyncTabParamsManager asyncTabParamsManager,
            Supplier<TabModelSelector> tabModelSelectorSupplier,
            String startPageUrl,
            TaffyManualNavigationCoordinator manualNavigation) {
        super(
                activity,
                window,
                // A new factory per tab, which is upstream's own arrangement: the delegate a
                // factory hands out is per-tab state, and TabImpl destroys the one it was given.
                () -> new TaffyTabDelegateFactory(manualNavigation),
                profileProviderSupplier,
                incognito,
                asyncTabParamsManager,
                tabModelSelectorSupplier,
                NO_COMPOSITOR_VIEW_HOLDER);
        mWindow = window;
        mProfileProviderSupplier = profileProviderSupplier;
        mIsIncognito = incognito;
        mTabModelSelectorSupplier = tabModelSelectorSupplier;
        mStartPageUrl = startPageUrl;
        mManualNavigation = manualNavigation;
    }

    @Override
    public void setTabModel(TabModel tabModel) {
        super.setTabModel(tabModel);
        mTabModel = tabModel;
    }

    /**
     * Keeps the persistent store's no-state fallback live on low-memory devices.
     *
     * <p>When a saved state file cannot be read, {@code TabPersistentStoreImpl} calls this exact
     * overload with {@link TabLaunchType#FROM_RESTORE}. Upstream turns any background tab into a
     * lazy tab when {@code SysUtils.isLowEndDevice()} is true. Such a tab has no {@code
     * WebContents}, and this activity cannot enter upstream's {@code ChromeActivity}-only inflate
     * path, so it can never load. The other launch types stay entirely upstream-owned.
     */
    @Override
    public @Nullable Tab createNewTab(
            LoadUrlParams loadUrlParams,
            @TabLaunchType int type,
            @Nullable Tab parent,
            int position) {
        if (!TaffyTabRestore.requiresLiveNewTab(type)) {
            return super.createNewTab(loadUrlParams, type, parent, position);
        }
        return createLiveRestorationFallback(loadUrlParams, parent, position);
    }

    /** Covers callers using the shorter {@code TabCreator} overload. */
    @Override
    public @Nullable Tab createNewTab(
            LoadUrlParams loadUrlParams, @TabLaunchType int type, @Nullable Tab parent) {
        if (!TaffyTabRestore.requiresLiveNewTab(type)) {
            return super.createNewTab(loadUrlParams, type, parent);
        }
        int position = TabModel.INVALID_TAB_INDEX;
        TabModel model = mTabModel;
        if (model != null && parent != null) {
            int parentIndex = model.indexOf(parent);
            if (parentIndex != TabModel.INVALID_TAB_INDEX) position = parentIndex + 1;
        }
        return createLiveRestorationFallback(loadUrlParams, parent, position);
    }

    /** Covers callers which carry the title used only by upstream's lazy representation. */
    @Override
    public @Nullable Tab createNewTab(
            LoadUrlParams loadUrlParams,
            @Nullable String title,
            @TabLaunchType int type,
            @Nullable Tab parent,
            int position) {
        if (!TaffyTabRestore.requiresLiveNewTab(type)) {
            return super.createNewTab(loadUrlParams, title, type, parent, position);
        }
        return createLiveRestorationFallback(loadUrlParams, parent, position);
    }

    /** Covers the public intent-carrying entry used by {@code launchUrl}. */
    @Override
    public @Nullable Tab createNewTab(
            LoadUrlParams loadUrlParams,
            @TabLaunchType int type,
            @Nullable Tab parent,
            @Nullable Intent intent) {
        if (!TaffyTabRestore.requiresLiveNewTab(type)) {
            return super.createNewTab(loadUrlParams, type, parent, intent);
        }
        return createNewTab(loadUrlParams, type, parent);
    }

    /** History-copy creation must not reintroduce a lazy restoration tab either. */
    @Override
    public @Nullable Tab createTabWithHistory(Tab parent, @TabLaunchType int type) {
        if (!TaffyTabRestore.requiresLiveNewTab(type)) {
            return super.createTabWithHistory(parent, type);
        }
        return createNewTab(new LoadUrlParams(parent.getUrl()), type, parent);
    }

    private @Nullable Tab createLiveRestorationFallback(
            LoadUrlParams loadUrlParams, @Nullable Tab parent, int position) {
        TabModel model = mTabModel;
        ProfileProvider provider = mProfileProviderSupplier.get();
        if (model == null || provider == null) return null;

        Profile profile = ProfileProvider.getOrCreateProfile(provider, mIsIncognito);
        Tab tab =
                TabBuilder.createLiveTab(profile, /* initiallyHidden= */ true)
                        .setWindow(mWindow)
                        .setParent(parent)
                        .setLaunchType(TabLaunchType.FROM_RESTORE)
                        .setDelegateFactory(new TaffyTabDelegateFactory(mManualNavigation))
                        .setInitiallyHidden(true)
                        .build();
        String candidate =
                TaffyTabRestore.fallbackUrlFor(loadUrlParams.getUrl(), null, mStartPageUrl);
        GURL parsed = new GURL(candidate);
        String url = parsed.isValid() && !parsed.isEmpty() ? parsed.getSpec() : mStartPageUrl;
        tab.loadUrl(new LoadUrlParams(url, PageTransition.GENERATED));
        model.addTab(
                tab,
                position,
                TabLaunchType.FROM_RESTORE,
                TabCreationState.LIVE_IN_BACKGROUND);
        return tab;
    }

    /**
     * Creates one regular task-owned tab and marks it before model publication.
     *
     * <p>The ordinary creator loads and publishes inside one call, so an observer can classify the
     * tab before a caller gets it back. This dedicated path deliberately separates build, claim,
     * publication and load. A failed claim destroys the still-unpublished tab; it is never briefly
     * visible as a user tab and never reaches a source-selection observer.
     */
    public @Nullable Tab createTaskOwnedTab(
            String exactAddress, Predicate<Tab> claimBeforePublish) {
        GURL address = new GURL(exactAddress);
        if (!address.isValid()
                || !("http".equals(address.getScheme()) || "https".equals(address.getScheme()))
                || !address.getUsername().isEmpty()
                || !address.getPassword().isEmpty()
                || !exactAddress.equals(address.getSpec())) {
            return null;
        }
        Tab tab = createClaimedTaskTab(claimBeforePublish);
        if (tab == null) return null;
        publishTaskTab(tab);
        tab.loadUrl(new LoadUrlParams(exactAddress, PageTransition.AUTO_TOPLEVEL));
        return tab;
    }

    /**
     * Creates one regular tab for an errand, marked before model publication.
     *
     * <p>An errand page is a page a person was <i>sent</i> to rather than one they browsed to: a
     * vendor's sign-in, the page a key is fetched from, a vendor's own documentation. Screen
     * SCR-110 draws it with a toolbar and nothing else — no address bar, no action row — and no
     * surface counts it or lists it in the switcher.
     *
     * <p><b>It is nonetheless an ordinary tab in the ordinary model, and that is load-bearing
     * rather than a shortcut.</b> Two things depend on it. An errand page can reach an address
     * that is not a web address — a vendor's own app scheme, {@code mailto:}, {@code tel:} — and
     * the only path that hands one to Android is {@code
     * TaffyManualNavigationCoordinator.openExternalApp}, which begins {@code isCurrent(sourceTab)}
     * — {@code selector.getCurrentTab() == tab}. A tab in no model is never the current tab, so on
     * a model-less errand page every such address would be silently dropped. The second is smaller
     * and would have been a slow puzzle: {@code TabImpl} starts hidden and is uncovered only by
     * {@code show()}, which the model does on selection and nothing else would.
     *
     * <p>The instance this paragraph used to name is gone, and is worth one sentence because it
     * was the one that came <i>back</i>. The account plane's sign-in returned through {@code
     * com.taffygo.browser://auth}, so a dropped hand-off there was a sign-in that started and
     * could never finish. Decision 0200 removed that plane's servers, the Android ingress went
     * with it, and Chromium patch 0022 no longer registers the filter, so nothing in the product
     * emits or receives that URI. Provider sign-in never travelled this road: its redirect is
     * claimed by a navigation throttle inside the page (decision 0095), which is a navigation and
     * not an intent.
     *
     * <p>The claim runs before publication for the same reason {@link #createTaskOwnedTab}'s does:
     * an observer must never see this tab as one of the person's own, not even for the moment
     * between publication and the first projection.
     *
     * <p>The address rule is narrower than {@link #createTaskOwnedTab}'s in one way and wider in
     * another. Narrower: {@code https} only, because every caller here hands over an address the
     * product itself resolved and an errand is never run over a cleartext hop. Wider: the
     * canonical spec is loaded rather than the string as it arrived, and the two are not required
     * to be identical — requiring that would refuse addresses that are merely spelled differently
     * from their canonical form, which four shipped catalog links are. The query survives
     * canonicalisation untouched, which matters more here than usual: an authorization address is
     * mostly query, and {@code state} and {@code code_challenge} live in it.
     */
    public @Nullable Tab createErrandTab(String exactAddress, Predicate<Tab> claimBeforePublish) {
        GURL address = new GURL(exactAddress);
        if (!address.isValid()
                || address.isEmpty()
                || !"https".equals(address.getScheme())
                || address.getHost().isEmpty()
                || !address.getUsername().isEmpty()
                || !address.getPassword().isEmpty()) {
            return null;
        }
        Tab tab = createClaimedTaskTab(claimBeforePublish);
        if (tab == null) return null;
        publishTaskTab(tab);
        tab.loadUrl(new LoadUrlParams(address.getSpec(), PageTransition.AUTO_TOPLEVEL));
        return tab;
    }

    /** Creates only the opener-free opaque blank and reports it after that document commits. */
    public void createTaskOwnedDiscoveryTab(
            Predicate<Tab> claimBeforePublish, Callback<Tab> callback) {
        Tab tab = createClaimedTaskTab(claimBeforePublish);
        if (tab == null) {
            callback.onResult(null);
            return;
        }
        class DiscoveryLoadObserver extends EmptyTabObserver {
            private boolean mCompleted;

            private void complete(@Nullable Tab result) {
                if (mCompleted) return;
                mCompleted = true;
                tab.removeObserver(this);
                callback.onResult(result);
            }

            @Override
            public void onPageLoadFinished(Tab observedTab, GURL url) {
                WebContents contents = tab.getWebContents();
                boolean exact =
                        observedTab == tab
                                && "about:blank".equals(url.getSpec())
                                && contents != null
                                && "about:blank".equals(contents.getLastCommittedUrl().getSpec());
                complete(exact ? tab : null);
            }

            @Override
            public void onPageLoadFailed(Tab observedTab, int errorCode) {
                complete(null);
            }

            @Override
            public void onCrash(Tab observedTab) {
                complete(null);
            }

            @Override
            public void onDestroyed(Tab observedTab) {
                complete(null);
            }
        }
        DiscoveryLoadObserver observer = new DiscoveryLoadObserver();
        tab.addObserver(observer);
        publishTaskTab(tab);
        Tab.LoadUrlResult load =
                tab.loadUrl(new LoadUrlParams("about:blank", PageTransition.AUTO_TOPLEVEL));
        if (load.tabLoadStatus == Tab.TabLoadStatus.PAGE_LOAD_FAILED) {
            observer.complete(null);
        }
    }

    private @Nullable Tab createClaimedTaskTab(Predicate<Tab> claimBeforePublish) {
        ProfileProvider provider = mProfileProviderSupplier.get();
        if (mIsIncognito || mTabModel == null || provider == null || claimBeforePublish == null) {
            return null;
        }
        Profile profile = ProfileProvider.getOrCreateProfile(provider, /* incognito= */ false);
        Tab tab =
                TabBuilder.createLiveTab(profile, /* initiallyHidden= */ false)
                        .setWindow(mWindow)
                        .setLaunchType(TabLaunchType.FROM_CHROME_UI)
                        .setDelegateFactory(new TaffyTabDelegateFactory(mManualNavigation))
                        .setInitiallyHidden(false)
                        .build();
        if (!claimBeforePublish.test(tab)) {
            tab.destroy();
            return null;
        }
        return tab;
    }

    private void publishTaskTab(Tab tab) {
        mTabModel.addTab(
                tab,
                TabModel.INVALID_TAB_INDEX,
                TabLaunchType.FROM_CHROME_UI,
                TabCreationState.LIVE_IN_FOREGROUND);
    }

    /**
     * Rebuilds one saved tab, already holding its page.
     *
     * <p><b>Why {@code setTabState} is not called, when it is the obvious way to carry the saved
     * fields across.</b> {@code TabImpl.restoreFieldsFromState} assigns {@code mWebContentsState =
     * state.contentsState}, and on this path nothing ever clears it: {@code unfreezeContents} is
     * the only clearer and it is exactly what this method exists to avoid. {@code
     * TabStateExtractor.getWebContentsState} then takes its first branch — "the tab is still
     * frozen, we can just use the existing state" — for the rest of the tab's life, so every later
     * save would rewrite the buffer this tab was restored from and the session would freeze at the
     * moment of this launch. The saved fields are applied through {@code setPreInitializeAction}
     * instead, which {@code TabBuilder.build()} runs immediately before {@code tab.initialize(...)}
     * — the same point {@code restoreFieldsFromState} would have run at.
     *
     * <p>Two of them are not applied there. {@code isPinned} goes through {@code
     * TabBuilder.setInitialPinState}, because {@code TabImpl.initialize} assigns {@code mIsPinned}
     * from its own parameter <i>after</i> the pre-initialize callback and would overwrite it. The
     * address and title are not applied at all, because they do not need to be: with a live {@code
     * WebContents} in hand, {@code Tab.getUrl()} reads {@code getVisibleUrl()} and {@code
     * TabImpl.initialize} calls {@code updateTitle()}, so both come from the restored navigation
     * entry rather than from a cached copy of it.
     *
     * <p><b>Restoring eagerly is not a choice about latency.</b> {@code willOpenInForeground} is
     * hard-coded false for {@code FROM_RESTORE} in {@code TabModelOrderControllerImpl}, so every
     * restored tab is a background tab and upstream would defer the work to first show. Here there
     * is no first show that can do it, so "eager" and "never" are the only two options.
     *
     * <p>Reparenting is handed straight back to upstream: that branch produces a tab that is
     * already live and never touches the frozen state, so it is not this method's problem, and
     * reproducing it would be reproducing {@code ReparentingTask} for no reason. Nothing in this
     * build reparents a tab.
     */
    @Override
    public @Nullable Tab createFrozenTab(TabState state, int id, int index) {
        if (isReparenting(id)) return super.createFrozenTab(state, id, index);

        TabModel model = mTabModel;
        if (model == null) return null;

        ProfileProvider profileProvider = mProfileProviderSupplier.get();
        // TaffyTabModelHolder waits for the profile before it builds anything, and
        // TabModelSelectorImpl asserts the same thing one step earlier. Answering null rather than
        // dereferencing keeps a broken order a lost tab instead of a crash on some devices only.
        assert profileProvider != null : "createFrozenTab before the profile provider is fulfilled";
        if (profileProvider == null) return null;
        Profile profile = ProfileProvider.getOrCreateProfile(profileProvider, mIsIncognito);

        WebContentsState contentsState = state.contentsState;
        WebContents restored =
                contentsState == null
                        ? null
                        : contentsState.restoreWebContents(
                                profile, /* isHidden= */ true, /* noRenderer= */ true);

        Tab tab =
                restored != null
                        ? buildRestoredTab(profile, state, id, restored)
                        : buildTabForUnreadableState(profile, state, id, contentsState);

        // FROZEN_ON_RESTORE is kept for the tab that really was restored, even though it is no
        // longer frozen in upstream's sense, because that is the creation state TabStateAttributes
        // reads to mark a tab CLEAN. Calling a restored tab anything else would mark the whole
        // session dirty on every launch and rewrite every tab file for nothing. The tab whose state
        // could not be read is genuinely dirty, and says so.
        model.addTab(
                tab,
                index,
                TabLaunchType.FROM_RESTORE,
                restored != null
                        ? TabCreationState.FROZEN_ON_RESTORE
                        : TabCreationState.LIVE_IN_BACKGROUND);
        return tab;
    }

    /**
     * The ordinary case: the saved navigation history came back, and the tab is built around it.
     */
    private Tab buildRestoredTab(Profile profile, TabState state, int id, WebContents webContents) {
        return TabBuilder.createFromFrozenState(profile)
                .setId(id)
                .setWindow(mWindow)
                .setParent(parentOf(state))
                .setDelegateFactory(new TaffyTabDelegateFactory(mManualNavigation))
                .setInitiallyHidden(true)
                .setInitialPinState(state.isPinned)
                .setWebContents(webContents)
                .setPreInitializeAction(tab -> applySavedFields(tab, state, id))
                .build();
    }

    /**
     * The failure case, mirroring {@code TabImpl.unfreezeContents}'s own.
     *
     * <p>A tab whose navigation history cannot be read back still existed and still has a place in
     * the list, so it is rebuilt as an ordinary live tab pointed at the best address still known
     * about it rather than dropped. {@code PageTransition.GENERATED} is upstream's own answer for
     * this load: nobody typed it and nobody followed a link to it.
     */
    private Tab buildTabForUnreadableState(
            Profile profile, TabState state, int id, @Nullable WebContentsState contentsState) {
        Tab tab =
                TabBuilder.createLiveTab(profile, /* initiallyHidden= */ true)
                        .setLaunchType(TabLaunchType.FROM_RESTORE)
                        .setId(id)
                        .setWindow(mWindow)
                        .setParent(parentOf(state))
                        .setDelegateFactory(new TaffyTabDelegateFactory(mManualNavigation))
                        .setInitiallyHidden(true)
                        .setInitialPinState(state.isPinned)
                        .setPreInitializeAction(t -> applySavedFields(t, state, id))
                        .build();
        String url =
                TaffyTabRestore.fallbackUrlFor(
                        contentsState == null ? null : contentsState.getVirtualUrlFromState(),
                        contentsState == null
                                ? null
                                : contentsState.getFallbackUrlForRestorationFailure(),
                        mStartPageUrl);
        tab.loadUrl(new LoadUrlParams(url, PageTransition.GENERATED));
        return tab;
    }

    /**
     * Everything {@code TabImpl.restoreFieldsFromState} carries across that this class still can.
     *
     * <p>Runs before {@code TabImpl.initialize}, which is safe for all five: the observer list is
     * empty, so every notification below iterates nothing, and {@code setTabGroupId} documents in
     * upstream's own comment that it may be called before the native tab exists.
     */
    private static void applySavedFields(Tab tab, TabState state, int id) {
        tab.setTimestampMillis(state.timestampMillis);
        tab.setRootId(TaffyTabRestore.rootIdFor(state.rootId, id));
        tab.setTabGroupId(state.tabGroupId);
        tab.setUserAgent(state.userAgent);
        tab.setTabHasSensitiveContent(state.tabHasSensitiveContent);
    }

    /**
     * The tab this one was opened from, if it is still in the model.
     *
     * <p>Upstream reaches the same tab through {@code TabBuilder.setTabResolver}, which {@code
     * build()} consults only when a {@code TabState} was set — and this class does not set one. So
     * the lookup is done here instead, against the same selector and the same saved {@code
     * parentId}, with the same answer when the parent is gone: none.
     */
    private @Nullable Tab parentOf(TabState state) {
        TabModelSelector selector = mTabModelSelectorSupplier.get();
        return selector == null ? null : selector.getTabById(state.parentId);
    }
}

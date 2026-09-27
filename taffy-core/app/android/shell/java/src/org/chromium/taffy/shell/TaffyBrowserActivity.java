// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.shell;

import android.content.Context;
import android.content.Intent;
import android.content.res.Configuration;
import android.os.Build;
import android.os.Bundle;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.ViewGroup;
import android.view.WindowManager;
import android.widget.FrameLayout;
import android.widget.Toast;

import androidx.annotation.ColorInt;
import androidx.annotation.Nullable;

import com.taffygo.browser.ui.app.TaffyWindowComponent;

import org.chromium.base.Callback;
import org.chromium.base.IntentUtils;
import org.chromium.base.supplier.OneshotSupplier;
import org.chromium.chrome.R;
import org.chromium.chrome.browser.app.tabwindow.TabWindowManagerSingleton;
import org.chromium.chrome.browser.init.ActivityProfileProvider;
import org.chromium.chrome.browser.init.AsyncInitializationActivity;
import org.chromium.chrome.browser.multiwindow.MultiWindowUtils;
import org.chromium.chrome.browser.profiles.Profile;
import org.chromium.chrome.browser.profiles.ProfileProvider;
import org.chromium.chrome.browser.tab.Tab;
import org.chromium.chrome.browser.tab.TabLaunchType;
import org.chromium.chrome.browser.tabmodel.TabModelSelector;
import org.chromium.chrome.browser.tabmodel.TabModelSelectorTabObserver;
import org.chromium.chrome.browser.tabwindow.TabWindowManager;
import org.chromium.content_public.browser.WebContents;
import org.chromium.taffy.content.TaffyPageHostView;
import org.chromium.taffy.host.ChromiumTaffyProfileRuntimeProvider;
import org.chromium.taffy.host.TaffyAppLanguage;
import org.chromium.taffy.host.TaffyPageSurfaceBinding;
import org.chromium.taffy.host.TaffyProfileRuntimeProvider;
import org.chromium.taffy.host.TaffyShellNavigation;
import org.chromium.taffy.host.TaffyShellViews;
import org.chromium.ui.base.ActivityWindowAndroid;
import org.chromium.ui.base.WindowAndroid;
import org.chromium.ui.edge_to_edge.EdgeToEdgeSystemBarColorHelper;
import org.chromium.ui.modaldialog.DialogDismissalCause;
import org.chromium.ui.modaldialog.ModalDialogManager;

/**
 * The activity TaffyGo launches, and the first surface that is TaffyGo's rather
 * than Chrome's.
 *
 * <p>Decision 0024 makes every screen in the catalog TaffyGo's. The mechanism
 * is a class boundary that already exists upstream rather than a patch: at the
 * pinned milestone the chain is
 * {@code ChromeTabbedActivity -> ChromeActivity -> AsyncInitializationActivity
 * -> ChromeBaseAppCompatActivity}, and the useful seam sits between the second
 * and the third.
 * {@link AsyncInitializationActivity} carries browser-process startup
 * sequencing, native initialization, the profile supplier and the first-draw
 * metrics, and inflates no browser UI of its own; {@code ChromeActivity} is
 * where Chrome's toolbar, app menu and tab switcher begin. So TaffyGo extends
 * the third and never the second, which is why owning the whole interface costs
 * no upstream patch for the activity itself.
 *
 * <p><b>The old status-island gap is retired.</b> Patch 0009 once mounted a
 * separate status island in {@code ChromeTabbedActivity}, while patch 0022
 * launched this activity. That unreachable island was removed. The current
 * path is owned here: {@code openWindow} binds the selected tab through {@link
 * org.chromium.taffy.host.TaffySelectorTabBinding} and {@link
 * org.chromium.taffy.host.TaffyTabComponentOwner}; {@link
 * org.chromium.taffy.host.TaffyPageIntelligenceFactory} supplies selected-page
 * inspection to the TaffyGo surface. Observation still begins only from a
 * person-visible page action or explicitly delegated task; there is no hidden
 * launch-time page read and no legacy status-island control.
 *
 * <p><b>What this class does and does not own.</b> It creates the window and
 * the profile provider, inflates TaffyGo's interface, and joins the two halves
 * of a browser together: the page host goes into screen SCR-101's own content
 * slot through {@link TaffyPageSurfaceBinding}, and Chromium's tab model goes
 * behind the {@code BrowserMediator} seam every browsing surface already reads.
 * Every screen is compiled from {@code taffy-core/ui/android} through the
 * Kotlin mount, and the one thing this file knows about them is the name of the
 * function that returns a {@code View}.
 *
 * <p>It owns none of the machinery it joins. {@link TaffyTabModelHolder} owns
 * the tab model,
 * {@link ChromiumBrowserMediator} owns the projection of it, {@link
 * TaffyPageHostView} owns the compositor, and the profile-keyed Dagger runtime
 * owns everything the screens read. What is left here is lifetime: build in the
 * right order, hand each object to the one that needs it, and take them all
 * down in the reverse order. Every method below is one of those three.
 *
 * <p><b>What is still absent, stated rather than implied.</b> Manual downloads
 * are now projected from Chromium's profile-owned offline-content provider to
 * screen SCR-203, including pause and resume. Assistant download admission is
 * a different path: patch specification
 * {@code chromium/patches/0008-route-downloads-and-external-intents.md} remains
 * deferred with OD-056, so no Taffy action can start a download or app handoff.
 * A person's external-protocol link may leave only through a fresh physical
 * page-gesture proof and Android's chooser (see {@link TaffyExternalAppHandoff}).
 * Each Activity's selector is registered with {@code TabWindowManager}, so
 * Chromium owns window assignment, persistence identity, and tab movement
 * rather than an Activity-local substitute.
 *
 * <p><b>Why Java and not Kotlin.</b> The Kotlin mount is real and gated
 * ({@code android/tools/kotlin_mounts.py}), and it carries product sources
 * whose package is
 * {@code com.taffygo.browser.ui.*}. This class is upstream-shaped embedder
 * assembly in
 * {@code org.chromium.*}, it subclasses an upstream Java type, and it is the
 * one file the manifest names. Keeping it Java means the launcher entry point
 * does not depend on the mount being healthy.
 */
public class TaffyBrowserActivity extends AsyncInitializationActivity {
    /**
     * The address a tab opens on when nothing else says otherwise. A seam, and
     * named so it reads as one.
     *
     * <p>{@code about:blank} because it is still the only address this activity
     * can claim. A session is restored from disk now, so this is no longer what
     * most launches show — but a first run, and every new tab, has to land
     * somewhere, and there is no start-page setting to read yet. Loading anything
     * else would be this file inventing product behaviour that no screen, setting
     * or decision record has asked for.
     *
     * <p>It is emphatically not {@code chrome://newtab}: {@link
     * TaffyTabDelegateFactory} resolves no native page, so that address would be
     * fetched as an ordinary web page. TaffyGo's new tab is screen SCR-104, in
     * Compose, above this.
     *
     * <p>The literal itself lives in {@link
     * TaffyNavigationProjection#BLANK_PAGE}, because the projection has to
     * <i>recognise</i> the address this field opens tabs on in order to keep it
     * out of the words a person reads. Two literals would be two answers.
     */
    private static final String FIRST_PAGE_URL = TaffyNavigationProjection.BLANK_PAGE;

    /**
     * The view the browser's content is hosted in. TaffyGo's tree hangs off
     * this.
     */
    private ViewGroup mContentHost;

    /**
     * Where the page is drawn. Built before the first draw, live only after
     * native comes up.
     */
    private TaffyPageHostView mPageHost;

    /**
     * Every tab, its session on disk, and the creators that make more. Null
     * before native initialization has finished and after {@link #onDestroy}.
     */
    private @Nullable TaffyTabModelHolder mTabModelHolder;

    /** The tab model as this Window component's Taffy-owned screens see it. */
    private @Nullable ChromiumBrowserMediator mBrowserMediator;

    /**
     * Re-shows a tab whose content view was replaced under it. See {@link
     * #showTab}.
     */
    private @Nullable TabModelSelectorTabObserver mContentObserver;

    /**
     * How an inbound intent reaches the interface's back stack.
     *
     * <p>Built here and handed to the composition in {@link
     * #triggerLayoutInflation}, because the composition exists long before the
     * intent that needs it does. Opening the tab is only half of answering an
     * inbound link; the other half is that the person can see it, and this is the
     * only object in this file that can move the screen. See {@link
     * TaffyShellNavigation}.
     */
    private final TaffyShellNavigation mShellNavigation = new TaffyShellNavigation();

    /**
     * An address another application asked for, waiting for somewhere to put it.
     *
     * <p>A field rather than a parameter because the two events that produce it
     * and the one event that can act on it are not ordered with respect to each
     * other: an intent arrives either at launch or through {@link
     * #onNewIntentWithNative}, and a tab model exists only once the profile has
     * been fulfilled. Null whenever there is nothing owed, which is nearly
     * always.
     *
     * <p>What decides whether an arriving intent's address lands here is
     * {@link TaffyInboundArrival}, not this class: the rule differs between the
     * two arrivals and getting it wrong is invisible on a device until somebody
     * notices their links have stopped working.
     */
    private @Nullable String mPendingInboundUrl;

    /**
     * Words another application asked TaffyGo to search for, waiting for the same
     * thing.
     *
     * <p>A second field rather than a flag on the first, because the two are
     * answered by different machinery and only one of them is ever set — see
     * {@link TaffyInboundIntent}, which is where that exclusivity is decided.
     */
    private @Nullable String mPendingInboundQuery;

    /**
     * Draws whichever tab is selected. Held as a field because it has to be
     * removed again: an observer left on a process-lived supplier would keep this
     * activity alive after destroy.
     */
    private final Callback<Tab> mShowSelectedTab = this::showTab;

    /** Browser-owned process/profile composition. */
    private @Nullable TaffyProfileRuntimeProvider mProfileRuntimeProvider;

    /**
     * The regular-Dagger window graph. Its lifetime is closed exactly once by
     * this activity.
     */
    private @Nullable TaffyWindowComponent mWindowComponent;

    /**
     * Saved-state key for the Chromium-assigned persistence identity of this
     * Activity window.
     */
    private static final String WINDOW_ID_STATE = "taffy.window_id";

    /**
     * TaffyGo renders web content, so the GPU process starts with the activity
     * rather than lazily.
     *
     * <p>Returning false here is what a settings-style activity does; a browser
     * that deferred it would pay the start cost at the first navigation instead,
     * which is the frame a person is most likely to be looking at.
     */
    @Override
    public boolean shouldStartGpuProcess() {
        return true;
    }

    // -----------------------------------------------------------------------
    // The window: edge to edge, into the cutout, with TaffyGo's own bar icons.
    // -----------------------------------------------------------------------

    /**
     * The one object that may write this window's bar colours and icon polarity.
     *
     * <p>Captured rather than fetched. {@code getEdgeToEdgeManager()} is
     * {@code @VisibleForTesting} on {@code ChromeBaseAppCompatActivity}; the
     * supported route into the helper is {@link #initializeSystemBarColors},
     * which upstream calls once from
     * {@code onCreate} when {@link #canColorStatusBarWithEdgeToEdgeHelper()} is
     * true.
     *
     * <p>Null until then, and after {@code onDestroy} the helper upstream owns is
     * destroyed. Every use below is guarded.
     */
    private EdgeToEdgeSystemBarColorHelper mSystemBars;

    /**
     * Lets the window use the part of the screen the camera sits in.
     *
     * <p>Nothing upstream sets {@code layoutInDisplayCutoutMode} for this
     * activity — the attribute appears in no Chromium theme, and the only two
     * runtime setters
     * ({@code customtabs/features/ImmersiveModeController} and
     * {@code browser_ui/display_cutout/DisplayCutoutController}) are on paths
     * this activity does not take. So the window default {@code
     * LAYOUT_IN_DISPLAY_CUTOUT_MODE_DEFAULT} applies, which keeps the window
     * clear of the cutout and letterboxes it in landscape below Android 15.
     *
     * <p>This is the UI layer's {@code allowDrawingIntoTheCutout()}, in Java, for
     * the same reason it gives: making the ten releases behave as the newest one
     * does means a layout is verified once rather than twice. {@code ALWAYS}
     * arrived at API 30; {@code SHORT_EDGES} is the same permission for the top
     * and bottom only and is what API 29 has. minSdk is 29
     * ({@code build/config/android/config.gni:72}), so only API 29 takes the
     * second branch.
     *
     * <p><b>Why {@code onPreCreate} and not {@code onCreate}.</b>
     * {@code AsyncInitializationActivity.onCreate} is {@code protected final};
     * this is the hook it offers, it runs before {@code
     * ChromeBaseAppCompatActivity.onCreate} and therefore before anything reads
     * an inset, and {@code getWindow()} is already valid because the window is
     * made in {@code Activity.attach()}. It touches the window attributes only,
     * never
     * {@code getDecorView()}, which {@code applyThemeOverlays()} requires not to
     * have been called before it runs.
     */
    @Override
    protected void onPreCreate() {
        super.onPreCreate();
        WindowManager.LayoutParams attributes = getWindow().getAttributes();
        attributes.layoutInDisplayCutoutMode = Build.VERSION.SDK_INT >= Build.VERSION_CODES.R
                ? WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_ALWAYS
                : WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
        getWindow().setAttributes(attributes);
    }

    /**
     * The interface is built in the language screen SCR-407 recorded, from the
     * first resource this activity resolves.
     *
     * <p>Below API 33 only, and {@link org.chromium.taffy.host.TaffyAppLanguage}
     * is where the whole of the reasoning lives — including why this method does
     * nothing at all above that line. In one sentence: from API 33 up the
     * framework owns the per-app locale and Chromium defers to it ({@code
     * AppLocaleUtils.shouldUseSystemManagedLocale()}), so a second opinion here
     * could only contradict the one the browser process is already reading.
     *
     * <p><b>Why {@code applyOverrides} and not {@code onPreCreate}.</b> This is
     * upstream's own hook for exactly this, called from {@code
     * ChromeBaseAppCompatActivity.attachBaseContext} and feeding {@code
     * applyOverrideConfiguration} — so it lands before {@code onCreate}, before
     * {@code applyThemeOverlays()}, and before a single resource has been
     * resolved in the wrong language. {@code onPreCreate} runs after all of that.
     * It is {@code @CallSuper} and its
     * {@code super} carries night mode and the tablet width override, so the
     * result is combined rather than replaced.
     *
     * <p>Reading the preference here means opening the window component a few
     * milliseconds earlier than {@link #triggerLayoutInflation} would. That is
     * the same construction on the same thread — the store loads its file once,
     * synchronously, into a {@code StateFlow} — and it happens before the first
     * frame either way.
     */
    @Override
    protected boolean applyOverrides(Context baseContext, Configuration overrideConfig) {
        boolean result = super.applyOverrides(baseContext, overrideConfig);
        return TaffyAppLanguage.overrideConfiguration(baseContext, overrideConfig) || result;
    }

    /**
     * TaffyGo's screens pad their own edges, so upstream must not pad them first.
     *
     * <p><b>This one override is the whole defect.</b> With the default,
     * {@code ChromeBaseAppCompatActivity.setContentView(View)} wraps the content
     * in
     * {@code EdgeToEdgeLayoutCoordinator}, which registers as an inset consumer
     * on the window's
     * {@code InsetObserver}, pads that wrapper by {@code systemBars + ime}, and
     * then returns the insets with {@code statusBars}, {@code navigationBars},
     * {@code captionBar},
     * {@code displayCutout}, {@code tappableElement}, {@code
     * mandatorySystemGestures} and
     * {@code ime} all set to {@code Insets.NONE}. {@code InsetObserver}
     * dispatches that consumed value down the tree. {@code
     * WindowInsets.safeDrawing} is the union of exactly those types, so every
     * {@code TaffyEdges} value in every mounted screen resolves to zero and every
     * {@code windowInsetsPadding} call becomes a no-op — the window is
     * edge-to-edge and nothing TaffyGo draws ever reaches an edge. The same
     * wrapper paints the bar strips in Chrome's colours and paints the display
     * cutout black.
     *
     * <p>Returning false leaves the coordinator constructed but never
     * initialized: it is built at
     * {@code ChromeBaseAppCompatActivity:244}, but {@code ensureInitialized()} —
     * which inflates the layout and adds the inset consumer — is reached only
     * from {@code wrapContentView}. So no consumer is registered, the insets
     * arrive at the Compose tree intact, and
     * {@code removeInsetsConsumer} on destroy is a no-op loop over a table that
     * never held it.
     *
     * <p>Upstream does the same thing for the same reason: {@code
     * BaseCustomTabActivity:320-326} returns false for standalone webapps because
     * "wrapping the content view in EdgeToEdgeLayoutCoordinator adds system-bar
     * padding that prevents standalone/fullscreen webapp content from filling the
     * viewport".
     */
    @Override
    protected boolean wrapContentWithEdgeToEdgeLayout() {
        return false;
    }

    /**
     * Keeps the bar colours writable even if Chrome's edge-to-edge flag is ever
     * off.
     *
     * <p>The default is {@code EdgeToEdgeUtils.isEdgeToEdgeEverywhereEnabled()}.
     * That is true here — the cached flag's compiled default is true and the
     * native feature is enabled by default — so this changes nothing today. It
     * matters only in the configuration where the flag is false (an automotive
     * build, a device below API 35 on the manufacturer gate, a forced
     * {@code --disable-features=EdgeToEdgeEverywhere}): then {@code
     * canSetStatusBarColor()} would be false, {@code updateStatusBarColor()}
     * would return at its first line, and the status bar would keep the colour
     * and the icon polarity the night qualifier chose. With this override the
     * window is not edge-to-edge in that configuration but its bars are still
     * TaffyGo's, which is the correct degraded answer rather than a contradictory
     * one.
     */
    @Override
    protected boolean canColorStatusBarWithEdgeToEdgeHelper() {
        return true;
    }

    /**
     * Lets the status bar go transparent with no delegate helper behind it.
     *
     * <p>Insurance, and cheap. Today the delegate supplier is still fulfilled —
     * upstream builds the layout coordinator whether or not this activity wraps
     * its content with it, and the coordinator's {@code canSetStatusBarColor()}
     * is the interface default true — so
     * {@code updateStatusBarColor()} reaches {@code windowStatusBarColor =
     * Color.TRANSPARENT} anyway. What this guards is the obvious upstream
     * refactor: the day
     * {@code ChromeBaseAppCompatActivity:243} learns to skip the coordinator when
     * nothing wraps with it, the delegate becomes null, and without this the
     * window would paint an opaque status bar over an edge-to-edge tree. It also
     * makes
     * {@code EdgeToEdgeSystemBarColorHelper.ensureTransparencyWindowFlags()} run,
     * which clears
     * {@code FLAG_TRANSLUCENT_STATUS} / {@code FLAG_TRANSLUCENT_NAVIGATION} and
     * adds
     * {@code FLAG_DRAWS_SYSTEM_BAR_BACKGROUNDS} so transparency wins.
     */
    @Override
    protected boolean canSetTransparentStatusBarWithoutDelegate() {
        return true;
    }

    /**
     * Captures the helper, and lets upstream seed it from the theme.
     *
     * <p>The seed is Chrome's {@code android:statusBarColor}, which resolves
     * through the night qualifier and is therefore the wrong authority — but it
     * is the only authority that exists before TaffyGo's stored Appearance has
     * been read, and it is replaced by
     * {@link #onTaffySurfaceColorResolved} on the first composition. Calling
     * {@code super} keeps that seed explicit rather than leaving the helper on
     * whatever the window happened to hold.
     */
    @Override
    protected void initializeSystemBarColors(EdgeToEdgeSystemBarColorHelper helper) {
        mSystemBars = helper;
        super.initializeSystemBarColors(helper);
    }

    /**
     * The bars follow the Appearance setting, not the device.
     *
     * <p>Screen SCR-407 lets the Appearance setting disagree with the device, so
     * the icon polarity may not come from a night qualifier — and Chrome's theme
     * answers it from one twice over
     * ({@code @bool/window_light_status_bar}, and a different {@code
     * statusBarColor} again under
     * {@code values-sw600dp}). The colour handed in here is the surface the
     * composition has actually resolved and is actually painting, so polarity is
     * derived from the paint rather than from a parallel opinion about it.
     *
     * <p>The colour itself is never seen. Because the activity is edge-to-edge,
     * {@code EdgeToEdgeSystemBarColorHelper} sets the window's own status and
     * navigation bars to
     * {@code Color.TRANSPARENT} and turns contrast enforcement off; the value
     * survives only as the argument to {@code ColorUtils.calculateLuminance},
     * which picks the icons. That path already carries its own API-level guard —
     * {@code UiUtils.setStatusBarIconColor} uses
     * {@code WindowInsetsController.setSystemBarsAppearance} from API 30 and
     * falls back to
     * {@code SYSTEM_UI_FLAG_LIGHT_STATUS_BAR} below it — so nothing here needs
     * one.
     *
     * <p>Both setters early-return when the colour has not changed, so this is
     * free to be called on every recomposition that reaches it.
     */
    private void onTaffySurfaceColorResolved(@ColorInt int surfaceColor) {
        if (mSystemBars == null) return;
        mSystemBars.setStatusBarColor(surfaceColor);
        mSystemBars.setNavigationBarColor(surfaceColor);
        notifyTabsOfThemeChange();
    }

    /**
     * Tell every open page to re-read night and Dark sites from the profile.
     *
     * <p>Appearance can disagree with the device night qualifier, so Blink
     * will not see a configuration change. Walking the tab models is the
     * same notify Chrome issues on resume.
     */
    private void notifyTabsOfThemeChange() {
        TaffyTabModelHolder holder = mTabModelHolder;
        if (holder == null) return;
        TabModelSelector selector = holder.getTabModelSelector();
        for (int modelIndex = 0; modelIndex < selector.getModels().size(); modelIndex++) {
            org.chromium.chrome.browser.tabmodel.TabModel model =
                    selector.getModels().get(modelIndex);
            for (int tabIndex = 0; tabIndex < model.getCount(); tabIndex++) {
                Tab tab = model.getTabAt(tabIndex);
                if (tab == null) continue;
                WebContents contents = tab.getWebContents();
                if (contents != null) contents.notifyRendererPreferenceUpdate();
            }
        }
    }

    // -----------------------------------------------------------------------
    // The window and the profile: what a page needs before it can exist.
    // -----------------------------------------------------------------------

    /**
     * The activity's {@link WindowAndroid}, which is what native calls a "root
     * window".
     *
     * <p><b>Why this override is required rather than convenient.</b>
     * {@code AsyncInitializationActivity.createWindowAndroid()} returns null by
     * default and
     * {@code getWindowAndroid()} therefore answered null for the whole life of
     * this activity. A
     * {@code WindowAndroid} is not a nicety: it is the object the compositor is
     * bound to
     * ({@code ContentViewRenderView.onNativeLibraryLoaded} asserts it is
     * non-null), the object a
     * {@code WebContents} is given in {@code setDelegates}, and the object every
     * runtime permission request, intent result and keyboard-visibility signal is
     * routed through. Without it TaffyGo can draw its own interface and can never
     * draw a page.
     *
     * <p><b>Where the shape comes from.</b> {@code
     * SearchActivity.createWindowAndroid()} — the one other activity in the tree
     * that extends {@code AsyncInitializationActivity} directly and hosts content
     * of its own. It spells out the seven-argument constructor; the five-argument
     * overload used here delegates to that same constructor with
     * {@code ActivityAndroidPermissionDelegate}, {@code
     * ActivityKeyboardVisibilityDelegate} and
     * {@code activityTopResumedSupported = false}, which are exactly the values
     * SearchActivity passes by hand ({@code
     * ui/android/.../ActivityWindowAndroid.java:43-60}). Writing them out again
     * would be three more names for no difference in behaviour.
     *
     * <p><b>The window's dialog manager.</b> SearchActivity wraps the window in
     * an anonymous subclass overriding {@code getModalDialogManager()} so the
     * window can find the activity's dialog manager, which it builds in {@code
     * createModalDialogManager()}. This activity does the same, so a page's
     * dialogs and the browser's own reach one manager. See {@link
     * #createModalDialogManager()}.
     *
     * <p>{@code getIntentRequestTracker()} is constructed in this class's
     * constructor and
     * {@code getInsetObserver()} in {@code ChromeBaseAppCompatActivity.onCreate},
     * which runs as
     * {@code super.onCreate} immediately before the call site at
     * {@code AsyncInitializationActivity.java:454}. Both are therefore live here;
     * the inset observer asserts if it is not, which is the failure to want
     * rather than a silent null.
     */
    @Override
    protected ActivityWindowAndroid createWindowAndroid() {
        return new TaffyFileChooserWindow(this,
                /* listenToActivityState= */ true, getIntentRequestTracker(), getInsetObserver(),
                /* occlusionTrackingAllowed= */ true) {
            @Override
            public @Nullable ModalDialogManager getModalDialogManager() {
                return TaffyBrowserActivity.this.getModalDialogManager();
            }
        };
    }

    /**
     * The one dialog manager for this window: a page's dialogs ask the window
     * for it, and the browser's own ask the activity.
     *
     * <p>Upstream's default is null, and the window used to hold a manager of its
     * own while the activity held none. Chromium's download prompts read the
     * activity's. The one asking where to save cancelled the download instead
     * of asking; the ones for a file that can harm the device, a download over
     * plain HTTP and a file already downloaded call {@code showDialog} on the
     * null. Tapping a link to an {@code .apk} closed the browser with a
     * NullPointerException in {@code DangerousDownloadDialogBridge.showDialog},
     * on the phone on 2026-09-27, in the 1.0 build.
     *
     * <p>{@code ChromeBaseAppCompatActivity.onCreate} calls this before the
     * window exists and destroys what it returns in its own {@code onDestroy}.
     * {@link TaffyPageDialogs} is built from the context alone, so it can be made
     * this early.
     */
    @Override
    protected ModalDialogManager createModalDialogManager() {
        return TaffyPageDialogs.create(this);
    }

    /**
     * The profile this activity's pages belong to.
     *
     * <p>{@link ActivityProfileProvider} is upstream's own answer and the one
     * {@code ChromeTabbedActivity} uses. It registers as a {@code
     * ProfileManager.Observer}, sets itself as soon as the profile manager is
     * initialized — synchronously, if it already is — and unregisters on this
     * activity's destroy through the lifecycle dispatcher. Everything downstream
     * (the page below, and the tab model that follows) asks
     * {@code getProfileProviderSupplier().runSyncOrOnAvailable(...)} rather than
     * {@code get()}, so whether the profile happens to be ready at any particular
     * callback is not something this class has to know.
     */
    @Override
    protected OneshotSupplier<ProfileProvider> createProfileProvider() {
        return new ActivityProfileProvider(getLifecycleDispatcher());
    }

    /**
     * Where TaffyGo's own view tree is inflated, and the whole reason this seam
     * was chosen.
     *
     * <p>Nothing of Chrome's is above this point. The content host is a plain
     * container; TaffyGo's whole interface is the first child, and the compositor
     * and the content view attach into the same container beneath it by the
     * transfers that follow, in the order decision 0024 sets.
     *
     * <p>The interface is built here rather than in {@code onCreate} because this
     * is the callback
     * {@code AsyncInitializationActivity} guarantees runs before the first draw
     * and after the window exists. {@link TaffyShellViews#of} plants the
     * view-tree lifecycle, store and saved-state owners on the view it returns,
     * so it does not depend on
     * {@code setContentView} having run first — which at this point it has not.
     */
    @Override
    protected void triggerLayoutInflation() {
        // Upstream declares every compositor-hosting activity
        // android:hardwareAccelerated="false" in the manifest and turns it back
        // on at run time, so that the flag is off for the brief window before
        // an activity decides it wants it. ChromeActivity does that for its own
        // subclasses; this activity extends AsyncInitializationActivity
        // directly, so it has to make the call itself, exactly as
        // SearchActivity does from the same base class and at the same point.
        //
        // Without it the window is given no ThreadedRenderer and every Canvas
        // in the view tree is a software one. Most of the interface survives
        // that and merely draws slower, which is why nothing looked broken. A
        // TextureView does not: it draws nothing at all on a software canvas,
        // never reaches getTextureLayer(), and so never creates the
        // SurfaceTexture that onSurfaceTextureAvailable delivers. Screen
        // SCR-002's films therefore prepared a decoder, held it, and waited for
        // a surface that could not arrive -- a black box behind a poster, with
        // no error anywhere, because nothing had failed.
        enableHardwareAcceleration();
        mContentHost = new FrameLayout(this);
        mPageHost = new TaffyPageHostView(this);
        setContentView(mContentHost);
        onInitialLayoutInflationComplete();
    }

    /**
     * The container TaffyGo's surfaces attach into. Null before layout
     * inflation.
     */
    public ViewGroup getContentHost() {
        return mContentHost;
    }

    /**
     * Where the web page is drawn. Null before layout inflation.
     *
     * <p>Public because the Compose seam needs it: a {@code PageSurface}
     * implementation hands this view to {@code TaffyPageSurface}'s {@code
     * AndroidView}, which is how the page ends up in screen SCR-101's content
     * slot rather than behind the whole interface.
     */
    public TaffyPageHostView getPageHost() {
        return mPageHost;
    }

    /**
     * Attests only a real pointer release inside the visible page before the page handles it.
     * Renderer activation alone is insufficient: task actions never travel through this Android
     * input path, so they cannot mint the one-use proof required by popups or app handoff.
     */
    @Override
    public boolean dispatchTouchEvent(MotionEvent event) {
        TaffyTabModelHolder holder = mTabModelHolder;
        if (holder != null
                && mPageHost != null
                && TaffyManualGestureInput.isPageGesture(event, mPageHost)) {
            holder.recordManualPageGesture(event.getEventTime());
        }
        return super.dispatchTouchEvent(event);
    }

    /** Records a completed physical keyboard/switch activation while the page subtree has focus. */
    @Override
    public boolean dispatchKeyEvent(KeyEvent event) {
        TaffyTabModelHolder holder = mTabModelHolder;
        if (holder != null
                && mPageHost != null
                && TaffyManualGestureInput.isPageKeyActivation(event, mPageHost)) {
            holder.recordManualPageGesture(event.getEventTime());
        }
        return super.dispatchKeyEvent(event);
    }

    /**
     * Starts page/browser composition after native startup. The Rust core stays
     * in utility.
     */
    @Override
    public void finishNativeInitialization() {
        super.finishNativeInitialization();
        mProfileRuntimeProvider = ChromiumTaffyProfileRuntimeProvider.getStarted();
        startPageHost();
    }

    /**
     * Builds the compositor, then the tab model as soon as there is a profile.
     *
     * <p>This callback and not an earlier one because
     * {@code ContentViewRenderView.onNativeLibraryLoaded} is a JNI call and there
     * is no library to call into before this point.
     *
     * <p><b>The profile is asked for rather than assumed, and that is a
     * correctness requirement rather than caution.</b> {@code
     * TabModelSelectorImpl.onNativeLibraryReady} calls
     * {@code get()} on the profile supplier and asserts the result is non-null.
     * {@code ActivityProfileProvider} fulfils when the profile manager is
     * initialized, and whether that has happened by the time this callback runs
     * is not something this class can decide — so it uses {@code
     * runSyncOrOnAvailable}, which calls straight through if it has and waits if
     * it has not. Reading {@code get()} here would be an assertion failure that
     * appears only on some devices and some starts.
     */
    private void startPageHost() {
        ActivityWindowAndroid window = getWindowAndroid();
        assert window != null : "createWindowAndroid() returns non-null for this activity";
        mPageHost.initialize(window);
        getProfileProviderSupplier().runSyncOrOnAvailable(
                profileProvider -> startTabModel(window, profileProvider));
    }

    /**
     * Stands up the tab model and joins it to the interface.
     *
     * <p>Three edges are made here and they are made in this order because each
     * needs the one before it. The holder asks Chromium to assign the selector
     * and window id, then builds the persistent store on that id and starts
     * session restore. The mediator projects that selector into this Window
     * component, while the profile runtime owns Tab components independently of
     * this Activity. Finally the page host is pointed at whichever tab is
     * selected.
     *
     * <p><b>Why the first page is opened here and not by the store.</b> A
     * restored session already has tabs, and {@code restoreTabs} selects one;
     * this only runs on the launch where there was nothing to restore. Checking
     * the model rather than a "first run" flag means the browser opens a tab
     * exactly when it has none, which is also the right answer after every tab
     * has been closed.
     *
     * @param window the window the compositor was bound to in {@link
     *     #startPageHost}.
     */
    private void startTabModel(WindowAndroid window, ProfileProvider profileProvider) {
        // runSyncOrOnAvailable may fire after the activity is gone: the profile
        // manager is a process-wide singleton and this callback outlives nothing
        // but itself.
        if (isActivityFinishingOrDestroyed() || !mPageHost.isInitialized()) return;

        int requestedWindowId = restoredWindowId();
        if (requestedWindowId == TabWindowManager.INVALID_WINDOW_ID) {
            refuseAdditionalWindow();
            return;
        }
        TaffyTabModelHolder holder;
        try {
            holder = new TaffyTabModelHolder(
                    this, window, getProfileProviderSupplier(), FIRST_PAGE_URL, requestedWindowId);
        } catch (TaffyTabWindowUnavailableException unavailable) {
            refuseAdditionalWindow();
            return;
        }
        mTabModelHolder = holder;

        Profile originalProfile = profileProvider.getOriginalProfile();
        TabModelSelector selector = holder.getTabModelSelector();
        ChromiumBrowserMediator mediator = new ChromiumBrowserMediator(
                this, originalProfile, selector, holder.getTabCreatorManager(), FIRST_PAGE_URL,
                holder.getTabContentManager(), mShellNavigation::showErrandPage);
        mBrowserMediator = mediator;

        TaffyProfileRuntimeProvider runtimeProvider = mProfileRuntimeProvider;
        if (runtimeProvider == null) {
            throw new IllegalStateException("Chromium did not bind the Taffy profile runtime");
        }
        mWindowComponent = runtimeProvider.requireRegularRuntime(originalProfile)
                                   .openWindow(this, holder.getWindowId(), selector, mediator);
        mContentHost.addView(
                TaffyShellViews.of(this, mWindowComponent, this::onTaffySurfaceColorResolved,
                        TaffyPageSurfaceBinding.of(mPageHost), mShellNavigation));

        selector.getCurrentTabSupplier().addSyncObserverAndCallIfNonNull(mShowSelectedTab);

        // A tab restored from disk arrives frozen and builds its content view
        // later, and a tab whose renderer is replaced gets a new one. Neither
        // changes which tab is selected, so the supplier above says nothing — and
        // without this the page host would keep the view of a WebContents that is
        // gone.
        mContentObserver = new TabModelSelectorTabObserver(selector) {
            @Override
            public void onContentChanged(Tab tab) {
                if (tab == selector.getCurrentTab()) showTab(tab);
            }
        };

        // A cold start that arrived through TaffyInboundIntentActivity carries an
        // address, and it is claimed here rather than in onCreate because this is
        // the first moment there is a tab model to open it in. openInboundUrl
        // answers whether it did anything, so a launch with a link never also opens
        // a blank first tab and a launch without one still does.
        claimInbound(getIntent(), TaffyInboundArrival.Delivery.LAUNCH);
        if (openInboundUrl() || submitInboundQuery()) return;

        if (selector.getTotalTabCount() == 0) {
            holder.getTabCreatorManager()
                    .getTabCreator(/* incognito= */ false)
                    .launchUrl(FIRST_PAGE_URL, TabLaunchType.FROM_STARTUP);
        }
    }

    /**
     * A link tapped in another application while TaffyGo was already running.
     *
     * <p>Upstream's own deferral point, and the reason nothing here has to check
     * whether native is up: {@code AsyncInitializationActivity.onNewIntent} hands
     * the intent to
     * {@code NativeInitializationController}, which either calls this straight
     * through or queues it until initialization completes. What it does
     * <i>not</i> guarantee is a tab model — that is built one step further on,
     * when the profile arrives — so the address is parked and
     * {@link #startTabModel} claims it if this could not.
     *
     * <p><b>{@link TaffyInboundArrival.Delivery#WHILE_RUNNING} is the
     * load-bearing word here.</b> An intent handed to a running activity is never
     * a replay of one already acted on, and reading it under the launch intent's
     * replay guard is what made every link after the first configuration change
     * disappear without a word. Read that class before changing this line.
     */
    @Override
    public void onNewIntentWithNative(Intent intent) {
        super.onNewIntentWithNative(intent);
        claimInbound(intent, TaffyInboundArrival.Delivery.WHILE_RUNNING);
        if (!openInboundUrl()) submitInboundQuery();
    }

    @Override
    public void onRequestPermissionsResult(
            int requestCode, String[] permissions, int[] grantResults) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);
        TaffyWindowComponent component = mWindowComponent;
        if (component != null) {
            component.permissionAdapter().onRequestPermissionsResult(requestCode, grantResults);
            component.voiceInputAdapter().onRequestPermissionsResult(requestCode, grantResults);
        }
    }

    /**
     * Takes on what an intent that just arrived asks for, without dropping what
     * was already owed.
     *
     * <p>Both fields are settled together and by the same rule, because an intent
     * carries an address or a search and never both — {@link TaffyInboundIntent}
     * guarantees that — so the one it does not carry is null and simply leaves
     * the other field alone.
     *
     * @param intent the intent that arrived. Null is a real case: upstream's own
     *     callbacks admit it, and it means the same as an intent carrying
     *     nothing.
     * @param delivery how it arrived, which is the fact {@link
     *     TaffyInboundArrival} turns on and the one thing this method cannot work
     *     out for itself.
     */
    private void claimInbound(@Nullable Intent intent, TaffyInboundArrival.Delivery delivery) {
        boolean rebuiltFromSavedState = getSavedInstanceState() != null;
        mPendingInboundUrl = TaffyInboundArrival.owedAfter(delivery, rebuiltFromSavedState,
                mPendingInboundUrl, extraOf(intent, TaffyInboundIntent.EXTRA_URL));
        mPendingInboundQuery = TaffyInboundArrival.owedAfter(delivery, rebuiltFromSavedState,
                mPendingInboundQuery, extraOf(intent, TaffyInboundIntent.EXTRA_QUERY));
    }

    /** One extra off an intent that may not be there at all. */
    private static @Nullable String extraOf(@Nullable Intent intent, String name) {
        return intent == null ? null : IntentUtils.safeGetStringExtra(intent, name);
    }

    /**
     * Opens the parked address, if there is one and there is somewhere to put it.
     *
     * <p><b>A new tab, never the current one, and that is a product decision
     * rather than a convenience.</b> An inbound link must not destroy the page a
     * person was already reading, and nothing on this path carries a trust signal
     * that would justify reuse — upstream reuses a tab only when {@code
     * EXTRA_APP_ID} says the sender is the application that opened it, which is
     * an extra TaffyGo neither reads nor writes. {@code FROM_EXTERNAL_APP} is
     * upstream's own name for exactly this launch, so the tab records where it
     * came from in the vocabulary the rest of the tab model already speaks.
     *
     * <p><b>And the browsing surface comes forward with it.</b> Creating the tab
     * is only half of answering a link: with screen SCR-407 open, an inbound
     * intent used to load the page underneath Appearance and leave it there, so
     * the person's link did nothing they could see until they guessed their way
     * back to it. {@link TaffyShellNavigation} decides whether the stack may move
     * and this line asks it to.
     *
     * @return whether a tab was opened, so that a caller deciding between this
     *     and a blank first tab does not have to ask the same question twice.
     */
    private boolean openInboundUrl() {
        String url = mPendingInboundUrl;
        TaffyTabModelHolder holder = mTabModelHolder;
        if (url == null || holder == null) return false;
        mPendingInboundUrl = null;
        holder.getTabCreatorManager()
                .getTabCreator(/* incognito= */ false)
                .launchUrl(url, TabLaunchType.FROM_EXTERNAL_APP);
        mShellNavigation.showBrowsingSurface();
        return true;
    }

    /**
     * Answers a search that arrived from the system, the way TaffyGo's own
     * address bar answers one.
     *
     * <p>The decision is not made here and deliberately not made twice: {@link
     * TaffyInboundQuery} asks the repository's resolver — the same {@code
     * AddressBarResolver} that reads what a person types — and either hands back
     * a place to go or commits the interpretation, which is where TaffyGo's
     * honest "no search engine has been chosen" notice comes from. All this
     * method owns is the one thing the resolver cannot do from outside an
     * activity: open a tab.
     *
     * <p>A new tab, for the reason {@link #openInboundUrl()} gives. The address
     * is raw text and goes through {@code UrlFormatter.fixupUrl} inside the tab
     * creator, which is the same fixup every other new tab's address gets.
     *
     * <p>The browsing surface comes forward here too, and for a reason the
     * address case does not have: every answer a search can get is <i>read</i> on
     * screen SCR-101. A place to go opens a tab there; a refusal — TaffyGo has
     * chosen no search engine, which is OD-019 — is a notice that screen puts
     * into words. Left on some other screen, an honest refusal is
     * indistinguishable from the browser having ignored the person entirely.
     *
     * @return whether this did anything, so a caller can tell a search from a
     *     plain launch.
     */
    private boolean submitInboundQuery() {
        String query = mPendingInboundQuery;
        TaffyTabModelHolder holder = mTabModelHolder;
        TaffyWindowComponent component = mWindowComponent;
        if (query == null || holder == null || component == null) return false;
        mPendingInboundQuery = null;
        mShellNavigation.showBrowsingSurface();
        TaffyInboundQuery.submit(component.browserRepository(), component.lifetime().getScope(),
                query,
                host
                -> holder.getTabCreatorManager()
                        .getTabCreator(/* incognito= */ false)
                        .launchUrl(host, TabLaunchType.FROM_EXTERNAL_APP));
        return true;
    }

    /**
     * Points the page host at one tab, or at nothing, and starts the tab's
     * navigation if it has one waiting.
     *
     * <p>Both arguments to {@code showPage} come from the same tab and are
     * swapped together: the
     * {@code WebContents} is what the compositor draws and the view is what
     * touch, IME, text selection and accessibility arrive through. A tab whose
     * content view is replaced under it — a renderer that was killed and came
     * back — does not change which tab is selected, which is why {@link
     * #mContentObserver} exists.
     *
     * <p><b>Why the load is started here, and why here is the only place it can
     * be.</b> A
     * {@code WebContents} rebuilt from a saved session has its navigation entries
     * back and has not been asked to load them — {@code
     * WebContentsState::RestoreContentsFromByteBufferImpl} calls
     * {@code GetController().Restore(...)} and returns. Upstream starts that load
     * in
     * {@code TabImpl.restoreIfNeeded}, which is reached only through {@code
     * loadIfNeeded}, which returns false for this activity before it gets there
     * because {@code getActivity()} is not a
     * {@code ChromeActivity}. So without this the restored page would draw an
     * empty frame forever. The same guard would also permanently skip the
     * recovery after a renderer is killed, which is why the question is asked on
     * every show and not only on the first — {@code needsReload()} is true in
     * exactly those two cases and false the rest of the time.
     *
     * <p>The decision itself is {@link TaffyPageDisplay}, which has no browser in
     * it and is checked on a laptop. This method is the three calls that carry it
     * out.
     */
    private void showTab(@Nullable Tab tab) {
        if (mPageHost == null || !mPageHost.isInitialized()) return;
        WebContents webContents = tab == null ? null : tab.getWebContents();
        TaffyPageDisplay display = TaffyPageDisplay.of(
                tab != null, webContents != null, tab != null && tab.needsReload());
        if (display == TaffyPageDisplay.NOTHING) {
            mPageHost.showPage(null, null);
            return;
        }
        mPageHost.showPage(webContents, tab.getView());
        if (display == TaffyPageDisplay.PAGE_AND_START_LOAD) {
            webContents.getNavigationController().loadIfNecessary();
        }
    }

    /**
     * Writes the tab list before the process becomes a candidate for the
     * low-memory killer.
     *
     * <p>Where upstream's tabbed activity does it, and for the reason it does:
     * {@code onDestroy} is exactly the callback a killed process does not get, so
     * a session that is only saved there is a session that is lost the one time
     * it mattered.
     */
    @Override
    public void onStopWithNative() {
        if (mTabModelHolder != null) {
            mTabModelHolder.clearManualPageGesture();
            mTabModelHolder.saveState();
        }
        super.onStopWithNative();
    }

    @Override
    protected void onSaveInstanceState(Bundle outState) {
        TaffyTabModelHolder holder = mTabModelHolder;
        if (holder != null) outState.putInt(WINDOW_ID_STATE, holder.getWindowId());
        super.onSaveInstanceState(outState);
    }

    /**
     * Called only by the upstream mismatch protocol before this Activity's id is
     * reassigned.
     */
    void releaseTabStoreForWindowReassignment() {
        TaffyTabModelHolder holder = mTabModelHolder;
        if (holder != null) holder.releasePersistentStoreForWindowReassignment();
    }

    private int restoredWindowId() {
        Bundle state = getSavedInstanceState();
        Integer restored = state != null && state.containsKey(WINDOW_ID_STATE)
                ? state.getInt(WINDOW_ID_STATE)
                : null;
        return TaffyWindowIdAllocator.requestedId(restored, TabWindowManagerSingleton.getInstance(),
                MultiWindowUtils.getMaxInstances());
    }

    private void refuseAdditionalWindow() {
        Toast.makeText(this, getString(R.string.unsupported_number_of_windows), Toast.LENGTH_LONG)
                .show();
        finishAndRemoveTask();
    }

    /**
     * Takes it all down, in the reverse order it was built, and the order is not
     * interchangeable.
     *
     * <p><b>The seam is emptied first.</b> Uninstalling the mediator is what
     * stops the screens reading a tab model that is about to be destroyed; they
     * go back to reporting nothing, which is true from this moment on. Then the
     * observers come off — an observer left on a process-lived supplier would
     * keep this activity reachable — and then the mediator itself.
     *
     * <p><b>Then the page, and this half is a use-after-free if reversed.</b> The
     * compositor's root layer <i>is</i> the page's layer ({@code
     * content_view_render_view.cc}
     * {@code SetRootLayer(web_contents->GetNativeView()->GetLayer())}), so the
     * render view is released before the tabs that own those layers. {@code
     * TaffyPageHostView.destroy} detaches the content view without destroying it,
     * because the tab owns it and the tab is still alive at that point.
     *
     * <p>The tab model is last of the three, and everything is before {@code
     * super.onDestroy()}, which is where {@code AsyncInitializationActivity}
     * destroys the {@code WindowAndroid} every tab's {@code WebContents} is bound
     * to.
     */
    @Override
    protected void onDestroy() {
        // Dialogs close first, while the pages they belong to still exist. The
        // manager itself is destroyed by ChromeBaseAppCompatActivity.
        ModalDialogManager dialogs = getModalDialogManager();
        if (dialogs != null) {
            dialogs.dismissAllDialogs(DialogDismissalCause.ACTIVITY_DESTROYED);
        }
        if (mWindowComponent != null) {
            mWindowComponent.lifetime().close();
            mWindowComponent = null;
        }
        if (mTabModelHolder != null) {
            mTabModelHolder.getTabModelSelector().getCurrentTabSupplier().removeObserver(
                    mShowSelectedTab);
        }
        if (mContentObserver != null) {
            mContentObserver.destroy();
            mContentObserver = null;
        }
        if (mBrowserMediator != null) {
            mBrowserMediator.destroy();
            mBrowserMediator = null;
        }
        if (mPageHost != null) mPageHost.destroy();
        if (mTabModelHolder != null) {
            mTabModelHolder.destroy();
            mTabModelHolder = null;
        }
        mProfileRuntimeProvider = null;
        super.onDestroy();
    }

    /**
     * TaffyGo does not run Chrome's first-run experience, and this is the whole
     * of how that is said.
     *
     * <p><b>What was happening.</b> On a clean install the launcher resolved to
     * this activity,
     * {@code AsyncInitializationActivity.onCreate} asked {@link
     * #requiresFirstRunToBeCompleted} and handed the launch to {@code
     * FirstRunActivity}, which drew TaffyGo's own brand mark above the words
     * "Welcome to Chrome", a Google Terms of Service link, and a notice that
     * Chrome sends usage and crash data to Google. Only the second and later
     * launches reached TaffyGo, which is why it was invisible in every screenshot
     * but the first. Beyond being the wrong product's onboarding, it presented
     * another company's terms and telemetry disclosure under TaffyGo's mark in a
     * product that is proprietary to a different owner.
     *
     * <p><b>Why this costs no patch.</b> The obvious remedy was a new patch
     * against
     * {@code //chrome/android}. It is not needed: upstream already provides the
     * seam as a
     * {@code protected} method with exactly this meaning, and both of its call
     * sites — the
     * {@code onCreate} gate and the {@code onStart} consistency check that throws
     * {@code IllegalStateException} — test it before they act, so overriding it
     * here suppresses the flow completely and cannot leave the second check
     * contradicting the first. That is mechanism D of decision 0024, a downstream
     * override, and it is always preferable to spending patch budget.
     *
     * <p><b>What this does NOT cover, measured rather than assumed.</b>
     * Overriding this method suppresses the flow for launches that start
     * <i>here</i>. An {@code android.intent.action.VIEW} from another application
     * does not: it enters through {@code ChromeLauncherActivity}, whose
     * {@code dispatch()} calls {@code FirstRunFlowSequencer.launch} with no
     * override in the path. Measured on 2026-08-20 on a clean profile — top
     * resumed activity
     * {@code org.chromium.chrome.browser.firstrun.FirstRunActivity}, showing
     * "Welcome to Chrome" and a Google Terms of Service link. That is the owner's
     * original complaint, still live through a second door, and the product
     * answer is unchanged: an inbound link before onboarding should reach
     * TaffyGo's own sequence with the URL deferred.
     *
     * <p><b>Before measuring any of that, check the device's flags file.</b>
     * {@code /data/local/tmp/taffy-command-line} on the bring-up phone carries
     * {@code --disable-fre}, which makes {@code checkIfFirstRunIsNecessary}
     * answer no for every caller in the process. With it in place the flow
     * vanishes for a reason that has nothing to do with this class, and a run
     * that concludes "first run is skipped" has measured the flags file. Move it
     * aside, measure, put it back.
     *
     * <p><b>Why the "almost always wrong" note upstream carries does not
     * apply.</b> It is written for Chrome's own activities, where the flow is
     * where consent and sign-in happen. TaffyGo's first run is its own sequence —
     * SCR-001 onward in
     * {@code docs/design/screen-catalog.md} — which is built, is reachable from
     * this activity's very first frame, and carries TaffyGo's own words. Two
     * first-run flows in one product is the defect; running the one that belongs
     * to the product is not.
     */
    @Override
    protected boolean requiresFirstRunToBeCompleted(Intent intent) {
        return false;
    }
}

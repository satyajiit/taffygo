// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.shell;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNotSame;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertSame;
import static org.junit.Assert.assertTrue;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.when;

import android.content.Intent;

import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;

import org.chromium.base.test.BaseRobolectricTestRunner;
import org.chromium.chrome.browser.app.ChromeActivity;
import org.chromium.chrome.browser.tab.Tab;
import org.chromium.chrome.browser.tab.TabDelegateFactory;
import org.chromium.chrome.browser.tab.TabLaunchType;
import org.chromium.chrome.browser.tabmodel.TabModelSelector;
import org.chromium.components.external_intents.ExternalNavigationDelegate;
import org.chromium.components.external_intents.ExternalNavigationHandler;
import org.chromium.components.external_intents.ExternalNavigationHandler.OverrideUrlLoadingResultType;
import org.chromium.components.external_intents.ExternalNavigationParams;
import org.chromium.components.external_intents.InterceptNavigationDelegateImpl;
import org.chromium.content_public.browser.WebContents;
import org.chromium.ui.base.WindowAndroid;

import java.lang.reflect.Method;
import java.lang.reflect.Modifier;

/**
 * Screen-less rules for the one decision that made closing a tab fatal.
 *
 * <p>Two kinds of assertion, and they answer different questions.
 *
 * <p><b>The behavioural half</b> drives {@link TaffyTabDelegateFactory}, {@link
 * TaffyExternalNavigationHandler} and {@code TaffyExternalNavigationDelegate} directly over a
 * mocked {@code Tab}. Everything those three do is decidable on a laptop, because none of it
 * touches native: the factory hands back an object, the unconfigured handler fails closed, the
 * configured handler consumes one physical proof, and the delegate answers from tab facts or closed
 * inherited policy.
 *
 * <p><b>The reflection half</b> checks the upstream shapes the fix hangs off, in the idiom {@code
 * TaffyBrowserActivityWindowTest} established and for the same reason: each one can change under a
 * rebase without breaking a compile, and the failure that follows is a {@code NullPointerException}
 * inside upstream with no TaffyGo file in the stack. That is precisely how this defect arrived, so
 * it is precisely what a host test should be watching.
 *
 * <p><b>What this cannot prove:</b> that a tab closes cleanly on a device. Destroying a tab runs
 * {@code TabImpl.destroy}, its observer list, and the native association this handler now causes to
 * be made — none of which exist on a JVM. The evidence for that is a run on a phone.
 */
@RunWith(BaseRobolectricTestRunner.class)
public class TaffyExternalNavigationTest {

    private Tab mTab;
    private WindowAndroid mWindow;
    private WebContents mWebContents;

    @Before
    public void setUp() {
        mTab = mock(Tab.class);
        mWindow = mock(WindowAndroid.class);
        mWebContents = mock(WebContents.class);
        when(mTab.getWindowAndroid()).thenReturn(mWindow);
        when(mTab.getWebContents()).thenReturn(mWebContents);
        when(mTab.getLaunchType()).thenReturn(TabLaunchType.FROM_CHROME_UI);
    }

    // -----------------------------------------------------------------------
    // Every tab gets a handler; its test-only unconfigured form fails closed.
    // -----------------------------------------------------------------------

    @Test
    public void everyTabIsGivenAnExternalNavigationHandler() {
        // The whole of the crash. A null here leaves InterceptNavigationDelegateImpl with a
        // WebContents associated and no WebContentsObserver, and the tab's destroy dereferences
        // the observer that was never made.
        ExternalNavigationHandler handler =
                new TaffyTabDelegateFactory().createExternalNavigationHandler(mTab);

        assertNotNull(
                "a TaffyGo tab must be given a handler; null is what upstream reads as \"not"
                        + " ready\" and abandons the tab half-wired",
                handler);
        assertTrue(handler instanceof TaffyExternalNavigationHandler);
    }

    @Test
    public void eachTabGetsItsOwnHandler() {
        // Per-tab state, like the web contents delegate beside it: the handler holds the tab it
        // answers for, so a shared one would answer for the wrong tab after the first close.
        TabDelegateFactory factory = new TaffyTabDelegateFactory();
        assertNotSame(
                factory.createExternalNavigationHandler(mTab),
                factory.createExternalNavigationHandler(mTab));
    }

    @Test
    public void unconfiguredHandlerFailsClosed() {
        ExternalNavigationHandler handler = new TaffyExternalNavigationHandler(mTab);

        // A missing window coordinator is not permission to inherit Chrome's external-intent
        // policy. This constructor exists only for focused policy tests and fails closed.
        assertEquals(
                OverrideUrlLoadingResultType.NO_OVERRIDE,
                handler.shouldOverrideUrlLoading(null).getResultType());
    }

    @Test
    public void configuredHandlerRoutesOnePhysicallyAttestedNavigation() {
        when(mTab.getId()).thenReturn(7);
        TabModelSelector selector = mock(TabModelSelector.class);
        when(selector.getCurrentTab()).thenReturn(mTab);
        TaffyManualGestureGate gate = new TaffyManualGestureGate(() -> 100L);
        TaffyExternalAppHandoff handoff = new TaffyExternalAppHandoff(gate, ignored -> true);
        TaffyManualNavigationCoordinator navigation =
                new TaffyManualNavigationCoordinator(() -> selector, gate, handoff);
        navigation.recordPageGesture(/* occurredAtMillis= */ 90);
        ExternalNavigationParams params = mock(ExternalNavigationParams.class);
        var url = TaffyTestGurl.from("mailto:hello@example.test");
        when(params.getUrl()).thenReturn(url);
        when(params.isMainFrame()).thenReturn(true);
        when(params.isRendererInitiated()).thenReturn(true);
        when(params.hasUserGesture()).thenReturn(true);
        ExternalNavigationHandler handler = new TaffyExternalNavigationHandler(mTab, navigation);

        assertEquals(
                OverrideUrlLoadingResultType.OVERRIDE_WITH_EXTERNAL_INTENT,
                handler.shouldOverrideUrlLoading(params).getResultType());
        assertEquals(
                OverrideUrlLoadingResultType.NO_OVERRIDE,
                handler.shouldOverrideUrlLoading(params).getResultType());
    }

    // -----------------------------------------------------------------------
    // The policy behind it. Three refusals, and each names what it holds shut.
    // -----------------------------------------------------------------------

    @Test
    public void thePolicyRefusesEveryExternalIntentTwiceMore() {
        ExternalNavigationDelegate policy = policyOf(mTab);

        // Removing the handler's override alone must not open the door. These are the two
        // refusals inside upstream's own logic that would still be standing.
        assertTrue(policy.shouldDisableAllExternalIntents());
        assertTrue(policy.shouldDisableExternalIntentRequestsForUrl(null, null));
    }

    @Test
    public void noTabIsClosedBecauseAnIntentLaunched() {
        ExternalNavigationDelegate policy = policyOf(mTab);

        // True here reaches InterceptNavigationDelegateClientImpl.closeTab(), which opens with
        // assumeNonNull(mTab.getActivity()) over a value that is null for every tab in this
        // activity. See theActivityIsNotAChromeActivityWhichIsWhyClosingOnIntentIsRefused below.
        assertFalse(policy.canCloseTabOnIntentLaunch());

        // And the method that would do it is empty rather than absent: tab closure in TaffyGo is
        // decided in ChromiumBrowserMediator and nowhere else.
        policy.closeTab();
    }

    @Test
    public void theLeavingIncognitoQuestionIsAnsweredWithoutADialog() {
        ExternalNavigationDelegate policy = policyOf(mTab);

        // False here would hand the question to upstream's own dialog, shown through the
        // window's dialog manager, which exists for a page's own dialogs and not for this — see
        // TaffyPageDialogs.
        assertTrue(policy.hasCustomLeavingIncognitoDialog());

        boolean[] decision = {true};
        policy.presentLeavingIncognitoModalDialog(consent -> decision[0] = consent);
        assertFalse("a private tab does not leave the browser on nobody's say-so", decision[0]);
    }

    @Test
    public void inheritedRouteNeverAddsASecondChooser() {
        assertTrue(policyOf(mTab).shouldAvoidDisambiguationDialog(null));
    }

    @Test
    public void theProductClaimsNoSchemeOfItsOwn() {
        // Chrome answers googlechrome:// here. Whether TaffyGo ever wants such a scheme is a
        // product decision, and a value invented in a delegate would make it by accident.
        assertNull(policyOf(mTab).getSelfScheme());
    }

    @Test
    public void inheritedRouteWritesNoIntentMetadata() {
        ExternalNavigationDelegate policy = policyOf(mTab);
        Intent intent = new Intent(Intent.ACTION_VIEW);

        policy.maybeSetWindowId(intent);
        policy.maybeSetPendingReferrer(intent, null);
        policy.maybeSetRequestMetadata(intent, /* hasUserGesture= */ true, false);
        policy.maybeSetPendingIncognitoUrl(intent);

        // A page address written into an Android extra is a page address outside this process.
        // The bounded handoff rebuilds a fresh target outside this delegate. The closed inherited
        // route therefore has no reason to put page metadata into its input.
        assertNull("the closed inherited route must add no extras", intent.getExtras());
        assertEquals(0, intent.getFlags());
    }

    // -----------------------------------------------------------------------
    // What the policy reads from the tab, rather than inventing.
    // -----------------------------------------------------------------------

    @Test
    public void theWindowAndTheContentsComeFromTheTab() {
        ExternalNavigationDelegate policy = policyOf(mTab);

        // getWindowAndroid() is the one answer upstream dereferences during construction, so a
        // wrong one here is a crash before any policy is consulted.
        assertSame(mWindow, policy.getWindowAndroid());
        assertSame(mWebContents, policy.getWebContents());
    }

    @Test
    public void theTabsOwnStateIsReported() {
        when(mTab.isDestroyed()).thenReturn(false);
        when(mTab.isOffTheRecord()).thenReturn(true);
        ExternalNavigationDelegate policy = policyOf(mTab);

        assertTrue(policy.hasValidTab());
        assertTrue(policy.isIncognito());

        when(mTab.isDestroyed()).thenReturn(true);
        assertFalse(policyOf(mTab).hasValidTab());
    }

    @Test
    public void theLaunchTypeIsReadRatherThanAssumed() {
        // Live rather than theoretical: shouldReparentTab is the one method of upstream's handler
        // TaffyGo does not override, and it reads this answer.
        assertFalse(policyOf(mTab).wasTabLaunchedFromLinkCreatingNewForegroundTab());

        when(mTab.getLaunchType()).thenReturn(TabLaunchType.FROM_LONGPRESS_FOREGROUND);
        assertTrue(policyOf(mTab).wasTabLaunchedFromLinkCreatingNewForegroundTab());

        when(mTab.getLaunchType()).thenReturn(TabLaunchType.FROM_LONGPRESS_FOREGROUND_IN_GROUP);
        assertTrue(policyOf(mTab).wasTabLaunchedFromLinkCreatingNewForegroundTab());

        when(mTab.getLaunchType()).thenReturn(TabLaunchType.FROM_LINK_CREATING_NEW_WINDOW);
        assertFalse(policyOf(mTab).wasTabLaunchedFromLinkCreatingNewForegroundTab());
        assertTrue(policyOf(mTab).wasTabLaunchedFromLinkCreatingNewWindow());
    }

    // -----------------------------------------------------------------------
    // The upstream shapes this fix hangs off. Each can change under a rebase
    // without breaking a compile, and each failure is a crash in upstream.
    // -----------------------------------------------------------------------

    @Test
    public void upstreamStillPairsTheHandlerWithTheObserver() throws NoSuchFieldException {
        // The two fields InterceptNavigationDelegateImpl.associateWithWebContents keeps in step,
        // and the reason a null handler is fatal rather than merely inert: it returns between
        // them, leaving the second unset while the first call site assumes it was set.
        assertNotNull(
                InterceptNavigationDelegateImpl.class.getDeclaredField("mExternalNavHandler"));
        assertNotNull(
                InterceptNavigationDelegateImpl.class.getDeclaredField("mWebContentsObserver"));
    }

    @Test
    public void upstreamStillAsksTheEmbedderOnEveryContentChange() throws NoSuchMethodException {
        // The method the tab's own observer calls on content change and again on destroy. If it
        // stops being public, or stops taking a WebContents, the sequence this fix reasons about
        // is no longer the sequence that runs.
        Method associate =
                InterceptNavigationDelegateImpl.class.getMethod(
                        "associateWithWebContents", WebContents.class);
        assertTrue(Modifier.isPublic(associate.getModifiers()));
    }

    @Test
    public void theRefusalIsStillAnOverride() throws NoSuchMethodException {
        Method upstream =
                ExternalNavigationHandler.class.getDeclaredMethod(
                        "shouldOverrideUrlLoading", ExternalNavigationParams.class);
        Method taffy =
                TaffyExternalNavigationHandler.class.getDeclaredMethod(
                        "shouldOverrideUrlLoading", ExternalNavigationParams.class);

        // A final method upstream would make TaffyGo's version a compile error rather than a
        // silent no-op, but the reverse is the risk worth naming: a signature change upstream
        // turns the override below into a new method that nothing ever calls, and every
        // navigation would then be decided by Chrome's policy instead of this product's.
        assertFalse(Modifier.isFinal(upstream.getModifiers()));
        assertTrue(Modifier.isPublic(upstream.getModifiers()));
        assertEquals(upstream.getReturnType(), taffy.getReturnType());
    }

    @Test
    public void upstreamStillTakesTheDelegateThisPolicyImplements() throws NoSuchMethodException {
        // The one-argument construction TaffyExternalNavigationHandler performs. An added
        // parameter here is the difference between a handler TaffyGo builds and one it cannot.
        assertNotNull(
                ExternalNavigationHandler.class.getConstructor(ExternalNavigationDelegate.class));
    }

    @Test
    public void theActivityIsNotAChromeActivityWhichIsWhyClosingOnIntentIsRefused()
            throws ClassNotFoundException, NoSuchMethodException {
        // TabImpl.getActivity() answers a ChromeActivity or null, and this activity is not one —
        // not being one is what makes owning every screen cost no upstream patch (decision 0024).
        // That is the whole reason canCloseTabOnIntentLaunch() must stay false: the close path
        // behind it dereferences this method's result through a runtime-empty assumeNonNull.
        Class<?> tabImpl =
                Class.forName(
                        "org.chromium.chrome.browser.tab.TabImpl",
                        /* initialize= */ false,
                        TaffyExternalNavigationTest.class.getClassLoader());
        assertEquals(
                ChromeActivity.class, tabImpl.getDeclaredMethod("getActivity").getReturnType());
        assertFalse(
                "if TaffyBrowserActivity ever becomes a ChromeActivity, the reasoning in"
                        + " TaffyExternalNavigationDelegate has to be redone rather than kept",
                ChromeActivity.class.isAssignableFrom(TaffyBrowserActivity.class));
    }

    private static ExternalNavigationDelegate policyOf(Tab tab) {
        return new TaffyExternalNavigationHandler(tab).getDelegateForTesting();
    }
}

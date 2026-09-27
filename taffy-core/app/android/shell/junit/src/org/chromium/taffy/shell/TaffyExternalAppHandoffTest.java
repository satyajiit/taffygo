// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package org.chromium.taffy.shell;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.when;

import android.app.Activity;
import android.content.Intent;

import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.robolectric.Robolectric;
import org.robolectric.Shadows;

import org.chromium.base.test.BaseRobolectricTestRunner;
import org.chromium.chrome.browser.tab.Tab;
import org.chromium.components.external_intents.ExternalNavigationParams;

@RunWith(BaseRobolectricTestRunner.class)
public class TaffyExternalAppHandoffTest {
    private final long[] mNow = {1_000};
    private final TaffyManualGestureGate mGate = new TaffyManualGestureGate(() -> mNow[0]);
    private final RecordingLauncher mLauncher = new RecordingLauncher();
    private final TaffyExternalAppHandoff mHandoff = new TaffyExternalAppHandoff(mGate, mLauncher);
    private Tab mTab;

    @Before
    public void setUp() {
        mTab = mock(Tab.class);
        when(mTab.getId()).thenReturn(7);
    }

    @Test
    public void physicalGestureLaunchesOnlySanitizedTarget() {
        mGate.record(/* tabId= */ 7, /* occurredAtMillis= */ 990);

        assertTrue(mHandoff.open(mTab, eligible("mailto:hello@example.test")));
        assertEquals(Intent.ACTION_VIEW, mLauncher.mTarget.getAction());
        assertEquals("mailto", mLauncher.mTarget.getData().getScheme());
        assertTrue(mLauncher.mTarget.getCategories().contains(Intent.CATEGORY_BROWSABLE));
        assertNull(mLauncher.mTarget.getPackage());
        assertNull(mLauncher.mTarget.getComponent());
        assertNull(mLauncher.mTarget.getExtras());
        assertEquals(0, mLauncher.mTarget.getFlags());
    }

    @Test
    @SuppressWarnings("deprecation")
    public void productionLauncherUsesChooserWithoutAReusableFallback() {
        Activity activity = Robolectric.buildActivity(Activity.class).setup().get();
        TaffyManualGestureGate gate = new TaffyManualGestureGate(() -> 1_000L);
        TaffyExternalAppHandoff handoff = new TaffyExternalAppHandoff(activity, gate);
        gate.record(/* tabId= */ 7, /* occurredAtMillis= */ 990);

        assertTrue(handoff.open(mTab, eligible("mailto:hello@example.test")));
        Intent chooser = Shadows.shadowOf(activity).getNextStartedActivity();
        assertEquals(Intent.ACTION_CHOOSER, chooser.getAction());
        Intent target = chooser.getParcelableExtra(Intent.EXTRA_INTENT);
        assertNotNull(target);
        assertEquals(Intent.ACTION_VIEW, target.getAction());
        assertEquals("mailto", target.getData().getScheme());

        // The chooser has no result callback into this adapter. Whether a person selects an app or
        // cancels it, the same page gesture cannot be used for a second launch or browser fallback.
        assertFalse(handoff.open(mTab, eligible("mailto:second@example.test")));
    }

    @Test
    public void finishingActivityCannotLaunchChooser() {
        Activity activity = Robolectric.buildActivity(Activity.class).setup().get();
        TaffyManualGestureGate gate = new TaffyManualGestureGate(() -> 1_000L);
        TaffyExternalAppHandoff handoff = new TaffyExternalAppHandoff(activity, gate);
        gate.record(/* tabId= */ 7, /* occurredAtMillis= */ 990);
        activity.finish();

        assertFalse(handoff.open(mTab, eligible("geo:12.3,45.6")));
        assertNull(Shadows.shadowOf(activity).getNextStartedActivity());
    }

    @Test
    public void intentUriLosesPackageFallbackExtrasAndFlags() {
        Intent target =
                TaffyExternalAppHandoff.sanitizedTarget(
                        TaffyTestGurl.from(
                                "intent://scan/#Intent;scheme=zxing;package=com.example.bad;"
                                        + "S.browser_fallback_url=https%3A%2F%2Fevil.test%2F;"
                                        + "launchFlags=0x10000000;end"));

        assertNotNull(target);
        assertEquals("zxing", target.getData().getScheme());
        assertNull(target.getPackage());
        assertNull(target.getExtras());
        assertEquals(0, target.getFlags());
    }

    @Test
    public void webAndLocalDataSchemesNeverLeave() {
        assertNull(
                TaffyExternalAppHandoff.sanitizedTarget(
                        TaffyTestGurl.from("https://example.test/")));
        assertNull(
                TaffyExternalAppHandoff.sanitizedTarget(TaffyTestGurl.from("file:///tmp/secret")));
        assertNull(
                TaffyExternalAppHandoff.sanitizedTarget(
                        TaffyTestGurl.from("content://authority/item")));
        assertNull(
                TaffyExternalAppHandoff.sanitizedTarget(TaffyTestGurl.from("javascript:alert(1)")));
    }

    @Test
    public void rendererActivationWithoutAndroidGestureCannotLaunch() {
        assertFalse(mHandoff.open(mTab, eligible("geo:12.3,45.6")));
        assertNull(mLauncher.mTarget);
    }

    @Test
    public void rejectedIntentCandidateStillConsumesPhysicalProof() {
        mGate.record(/* tabId= */ 7, /* occurredAtMillis= */ 990);

        assertFalse(
                mHandoff.open(
                        mTab,
                        eligible("intent://example/#Intent;scheme=https;package=bad.test;end")));
        assertFalse(mHandoff.open(mTab, eligible("geo:12.3,45.6")));
        assertNull(mLauncher.mTarget);
    }

    @Test
    public void ordinaryWebNavigationDoesNotSpendExternalProof() {
        mGate.record(/* tabId= */ 7, /* occurredAtMillis= */ 990);

        assertFalse(mHandoff.open(mTab, eligible("https://example.test/")));
        assertTrue(mHandoff.open(mTab, eligible("geo:12.3,45.6")));
    }

    @Test
    public void privateBackgroundAndApiNavigationsFailBeforeClaim() {
        mGate.record(/* tabId= */ 7, /* occurredAtMillis= */ 990);
        when(mTab.isOffTheRecord()).thenReturn(true);
        assertFalse(mHandoff.open(mTab, eligible("tel:+123")));

        when(mTab.isOffTheRecord()).thenReturn(false);
        ExternalNavigationParams background = eligible("tel:+123");
        when(background.isBackgroundTabNavigation()).thenReturn(true);
        assertFalse(mHandoff.open(mTab, background));

        ExternalNavigationParams api = eligible("tel:+123");
        when(api.isFromIntent()).thenReturn(true);
        assertFalse(mHandoff.open(mTab, api));
        assertNull(mLauncher.mTarget);
    }

    @Test
    public void chooserLaunchFailureIsFinalForThatGesture() {
        mLauncher.mResult = false;
        mGate.record(/* tabId= */ 7, /* occurredAtMillis= */ 990);
        ExternalNavigationParams params = eligible("market://details?id=example");

        assertFalse(mHandoff.open(mTab, params));
        mLauncher.mResult = true;
        assertFalse(mHandoff.open(mTab, params));
    }

    @Test
    public void closeRevokesPendingGesture() {
        mGate.record(/* tabId= */ 7, /* occurredAtMillis= */ 990);
        mHandoff.close();

        assertFalse(mHandoff.open(mTab, eligible("sms:+123")));
    }

    private static ExternalNavigationParams eligible(String address) {
        ExternalNavigationParams params = mock(ExternalNavigationParams.class);
        var url = TaffyTestGurl.from(address);
        when(params.getUrl()).thenReturn(url);
        when(params.isMainFrame()).thenReturn(true);
        when(params.isRendererInitiated()).thenReturn(true);
        when(params.hasUserGesture()).thenReturn(true);
        return params;
    }

    private static final class RecordingLauncher
            implements TaffyExternalAppHandoff.ChooserLauncher {
        private Intent mTarget;
        private boolean mResult = true;

        @Override
        public boolean launch(Intent sanitizedTarget) {
            mTarget = sanitizedTarget;
            return mResult;
        }
    }
}
